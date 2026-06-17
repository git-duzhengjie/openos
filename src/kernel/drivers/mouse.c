/* ============================================================
 * openos - PS/2 鼠标驱动 (IRQ 12)
 * ============================================================ */

#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "serial.h"
#include "string.h"

#define PS2_DATA_PORT     0x60
#define PS2_CMD_PORT      0x64

#define PS2_CMD_ENABLE_AUX    0xA8
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_MOUSE_ENABLE      0xF4
#define PS2_MOUSE_SET_SAMPLE  0xF3
#define PS2_MOUSE_GET_ID      0xF2
#define PS2_MOUSE_ACK         0xFA

#define PS2_CONFIG_IRQ1_ENABLE      0x01
#define PS2_CONFIG_IRQ12_ENABLE     0x02
#define PS2_CONFIG_FIRST_PORT_CLOCK 0x10
#define PS2_CONFIG_AUX_PORT_CLOCK   0x20

static mouse_state_t g_mouse;

static void mouse_write(uint8_t val);

static void mouse_wait_input() {
    int timeout = 10000;
    while (--timeout && (inb(PS2_CMD_PORT) & 2) != 0);
}

static void mouse_wait_output() {
    int timeout = 10000;
    while (--timeout && (inb(PS2_CMD_PORT) & 1) == 0);
}

static uint8_t mouse_read() {
    mouse_wait_output();
    return inb(PS2_DATA_PORT);
}

static uint8_t mouse_write_ack(uint8_t val) {
    mouse_write(val);
    return mouse_read();
}

static void mouse_set_sample_rate(uint8_t rate) {
    if (mouse_write_ack(PS2_MOUSE_SET_SAMPLE) != PS2_MOUSE_ACK) {
        return;
    }
    (void)mouse_write_ack(rate);
}

static int mouse_clamp_int(int value, int min, int max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void mouse_clamp_position(void) {
    /* bounds 未设置时用临时小范围（防止启动前坐标飞到无效区域） */
    int mx = (g_mouse.max_x <= 0) ? 1023 : g_mouse.max_x;
    int my = (g_mouse.max_y <= 0) ? 767 : g_mouse.max_y;
    g_mouse.x = mouse_clamp_int(g_mouse.x, 0, mx);
    g_mouse.y = mouse_clamp_int(g_mouse.y, 0, my);
}

static void mouse_write(uint8_t val) {
    mouse_wait_input();
    outb(PS2_CMD_PORT, 0xD4);
    mouse_wait_input();
    outb(PS2_DATA_PORT, val);
}

void mouse_irq_handle(void) {
    uint8_t data = inb(PS2_DATA_PORT);
    g_mouse.irq_count++;

    /*
     * 标准 PS/2 鼠标包第 1 字节 bit3 恒为 1。
     * 如果中途丢字节，必须在收第 1 字节阶段重新同步，否则会出现
     * 光标坐标突然跳动、点击命中位置与显示位置不一致等问题。
     */
    if (g_mouse.packet_index == 0 && !(data & 0x08)) {
        g_mouse.desync_count++;
        return;
    }

    g_mouse.packet[g_mouse.packet_index++] = data;
    if (g_mouse.packet_index < g_mouse.packet_size) {
        return;
    }

    g_mouse.packet_index = 0;
    uint8_t status = g_mouse.packet[0];
    if (!(status & 0x08)) {
        g_mouse.desync_count++;
        return;
    }

    int dx = (int)g_mouse.packet[1];
    int dy = (int)g_mouse.packet[2];
    int wheel = 0;
    if (g_mouse.packet_size >= 4) {
        wheel = (int8_t)(g_mouse.packet[3] & 0x0F);
        if (wheel & 0x08) {
            wheel |= 0xFFFFFFF0;
        }
    }

    /* 溢出包通常表示移动量不可用，直接丢弃避免坐标漂移 */
    if (status & 0xC0) {
        g_mouse.desync_count++;
        g_mouse.dx = 0;
        g_mouse.dy = 0;
        g_mouse.wheel = 0;
        g_mouse.buttons = status & 0x07;
        return;
    }

    if (status & 0x10) dx |= 0xFFFFFF00;
    if (status & 0x20) dy |= 0xFFFFFF00;
    dy = -dy; /* PS/2 向上为正，屏幕坐标向下为正 */

    g_mouse.packet_count++;
    g_mouse.dx = dx;
    g_mouse.dy = dy;
    g_mouse.wheel = wheel;
    g_mouse.z += wheel;
    g_mouse.buttons = status & 0x07;
    g_mouse.x += dx;
    g_mouse.y += dy;
    mouse_clamp_position();

    /* EOI 由 idt.c 的通用 irq_handler() 统一发送 */
}

void mouse_init(void) {
    memset(&g_mouse, 0, sizeof(mouse_state_t));
    /* 初始坐标范围设为 0，等待 gui_start() 通过 mouse_set_bounds() 设置真实分辨率。
     * clamp 时如果 max<=0 会 fallback 到 1023/767，避免启动前坐标完全失控。 */
    g_mouse.max_x = 0;
    g_mouse.max_y = 0;
    g_mouse.x = 0;
    g_mouse.y = 0;
    g_mouse.packet_size = 3;

    /* 使能辅助设备端口 */
    mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX);
    io_wait();

    /* 读取配置字节，使能鼠标中断 */
    mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    io_wait();
    uint8_t config = mouse_read();
    config |= PS2_CONFIG_IRQ1_ENABLE;  /* 保持键盘 IRQ1 打开 */
    config |= PS2_CONFIG_IRQ12_ENABLE; /* 启用 IRQ12 */
    config &= (uint8_t)~PS2_CONFIG_FIRST_PORT_CLOCK; /* 保持键盘时钟打开 */
    config &= (uint8_t)~PS2_CONFIG_AUX_PORT_CLOCK;   /* 启用鼠标时钟 */

    /* 写回配置 */
    mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    io_wait();
    mouse_wait_input();
    outb(PS2_DATA_PORT, config);
    io_wait();

    /* IntelliMouse 探测序列：200, 100, 80 后 GET_ID 返回 3 表示支持滚轮 4 字节包。 */
    mouse_set_sample_rate(200);
    mouse_set_sample_rate(100);
    mouse_set_sample_rate(80);
    uint8_t id = 0;
    if (mouse_write_ack(PS2_MOUSE_GET_ID) == PS2_MOUSE_ACK) {
        id = mouse_read();
        if (id == 3) {
            g_mouse.packet_size = 4;
        }
    }

    /* 启用鼠标采样 */
    mouse_write(PS2_MOUSE_ENABLE);
    mouse_wait_output();
    uint8_t ack = mouse_read();
    g_mouse.last_ack = ack;
    if (ack != PS2_MOUSE_ACK) {
        serial_write("[WARN] mouse init failed, no ack\n");
        g_mouse.present = 0;
        return;
    }

    /* IRQ12 在 idt.c 中固定直连到 mouse_irq_handle()，避免函数指针表损坏导致跳飞 */
    g_mouse.present = 1;
    serial_write("[OK] PS/2 mouse\n");
}

mouse_state_t *mouse_get_state(void) {
    return &g_mouse;
}

void mouse_snapshot_and_clear_delta(mouse_state_t *out) {
    uint32_t flags;
    if (!out) return;

    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    *out = g_mouse;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

void mouse_set_bounds(int width, int height) {
    if (width <= 0 || height <= 0) return;
    g_mouse.max_x = width - 1;
    g_mouse.max_y = height - 1;
    mouse_clamp_position();
}

void mouse_set_position(int x, int y) {
    g_mouse.x = x;
    g_mouse.y = y;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.wheel = 0;
    mouse_clamp_position();
}

void mouse_set_absolute_position(int x, int y, uint8_t buttons) {
    uint32_t flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");

    int old_x = g_mouse.x;
    int old_y = g_mouse.y;
    g_mouse.x = x;
    g_mouse.y = y;
    mouse_clamp_position();
    g_mouse.dx = g_mouse.x - old_x;
    g_mouse.dy = g_mouse.y - old_y;
    g_mouse.wheel = 0;
    g_mouse.buttons = buttons & 0x07;
    g_mouse.present = 1;
    g_mouse.absolute_mode = 1;
    g_mouse.packet_count++;

    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

void mouse_print_info(void) {
    serial_write("[MOUSE] PS/2 mouse status\n");
    serial_write(g_mouse.present ? "  present=yes\n" : "  present=no\n");
    serial_write("  buttons="); serial_write_hex(g_mouse.buttons); serial_write(" last_ack="); serial_write_hex(g_mouse.last_ack); serial_write("\n");
    serial_write("  irq_bytes="); serial_write_hex(g_mouse.irq_count); serial_write(" packets="); serial_write_hex(g_mouse.packet_count); serial_write(" desync="); serial_write_hex(g_mouse.desync_count); serial_write(" packet_size="); serial_write_hex((uint32_t)g_mouse.packet_size); serial_write("\n");
    serial_write("  pos="); serial_write_hex((uint32_t)g_mouse.x); serial_write(","); serial_write_hex((uint32_t)g_mouse.y); serial_write(" z="); serial_write_hex((uint32_t)g_mouse.z); serial_write(" wheel="); serial_write_hex((uint32_t)g_mouse.wheel); serial_write(" bounds="); serial_write_hex((uint32_t)g_mouse.max_x); serial_write(","); serial_write_hex((uint32_t)g_mouse.max_y); serial_write(" absolute="); serial_write_hex((uint32_t)g_mouse.absolute_mode); serial_write("\n");
}
