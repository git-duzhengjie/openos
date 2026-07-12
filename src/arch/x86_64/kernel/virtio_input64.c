/* ============================================================================
 * virtio_input64.c — virtio-input (virtio spec 5.8) 键盘/鼠标驱动
 *
 * 每个 virtio-input PCI function (1af4:1052) 暴露单个 eventq (queue 0)，
 * 设备主动把 evdev 事件 (type/code/value, 8 字节) 填入 driver 预投递的
 * write-only buffer。驱动 poll used 环，翻译成现有 GUI 注入通路，然后
 * 把 buffer 重新投递回 avail 环。
 *
 * 与 PS/2 共存：二者叠加注入同一 GUI 事件队列，互不干扰。
 * ==========================================================================*/
#include "virtio_input.h"
#include "virtio.h"
#include "virtio_modern.h"
#include "pci.h"
#include "pmm64.h"
#include "types.h"

/* GUI / 输入注入通路（复用 PS/2 已用接口） */
extern void gui_post_key_code_with_modifiers(int key_code, uint32_t modifiers);
extern void mouse_inject_relative(int dx, int dy, uint8_t buttons, int wheel);
extern void mouse_set_absolute_position_with_wheel(int x, int y, uint8_t buttons, int wheel);
extern void early_console64_write(const char *s);

#define VIRTIO_INPUT_VENDOR   0x1af4
#define VIRTIO_INPUT_DEVICE   0x1052
#define VINPUT_EVENTQ         0        /* queue index 0 = eventq */
#define VINPUT_MAX_DEVS       4        /* 最多探测 4 个输入设备 */
#define VINPUT_BUF_COUNT      16       /* 每设备预投递事件 buffer 数 */

/* ---- GUI 修饰键位（与 keyboard64.c KBD_MOD_* 对齐） ---- */
#define VIN_MOD_SHIFT  1u
#define VIN_MOD_CTRL   2u
#define VIN_MOD_ALT    4u

/* ---- 每设备状态 ---- */
typedef struct vinput_dev {
    virtio_modern_dev_t dev;
    virtqueue_t         vq;
    uint16_t            notify_off;
    uint8_t            *evt_buf;      /* VINPUT_BUF_COUNT 条事件缓冲(物理连续) */
    uint64_t            evt_phys;
    uint8_t             active;
    /* 鼠标聚合状态：一个 EV_SYN 周期内累积 */
    int                 acc_dx, acc_dy, acc_wheel;
    int                 abs_x, abs_y, have_abs;
    uint32_t            buttons;      /* 位0=左 位1=右 位2=中 */
    /* 键盘修饰键状态 */
    uint32_t            mods;
} vinput_dev_t;

static vinput_dev_t g_vin[VINPUT_MAX_DEVS];
static uint32_t     g_vin_count;

/* evdev 每条事件 8 字节，buffer 内按 index 排布 */
#define EVT_SZ  ((uint32_t)sizeof(virtio_input_event_t))

/* ---- evdev KEY_* -> GUI key code 映射 ---- */
/* GUI 使用 ASCII 主体 + 高位特殊键；此处覆盖常用键。 */
static int vin_map_key(uint16_t code, uint32_t mods) {
    int shift = (mods & VIN_MOD_SHIFT) != 0;
    switch (code) {
        /* 字母 KEY_A=30 ... 按 evdev 顺序，非 ASCII 序，需查表 */
        case 30: return shift ? 'A' : 'a';
        case 48: return shift ? 'B' : 'b';
        case 46: return shift ? 'C' : 'c';
        case 32: return shift ? 'D' : 'd';
        case 18: return shift ? 'E' : 'e';
        case 33: return shift ? 'F' : 'f';
        case 34: return shift ? 'G' : 'g';
        case 35: return shift ? 'H' : 'h';
        case 23: return shift ? 'I' : 'i';
        case 36: return shift ? 'J' : 'j';
        case 37: return shift ? 'K' : 'k';
        case 38: return shift ? 'L' : 'l';
        case 50: return shift ? 'M' : 'm';
        case 49: return shift ? 'N' : 'n';
        case 24: return shift ? 'O' : 'o';
        case 25: return shift ? 'P' : 'p';
        case 19: return shift ? 'R' : 'r';
        case 31: return shift ? 'S' : 's';
        case 20: return shift ? 'T' : 't';
        case 22: return shift ? 'U' : 'u';
        case 47: return shift ? 'V' : 'v';
        case 17: return shift ? 'W' : 'w';
        case 45: return shift ? 'X' : 'x';
        case 21: return shift ? 'Y' : 'y';
        case 44: return shift ? 'Z' : 'z';
        case 16: return shift ? 'Q' : 'q';
        /* 数字行 KEY_1=2 ... KEY_0=11 */
        case 2:  return shift ? '!' : '1';
        case 3:  return shift ? '@' : '2';
        case 4:  return shift ? '#' : '3';
        case 5:  return shift ? '$' : '4';
        case 6:  return shift ? '%' : '5';
        case 7:  return shift ? '^' : '6';
        case 8:  return shift ? '&' : '7';
        case 9:  return shift ? '*' : '8';
        case 10: return shift ? '(' : '9';
        case 11: return shift ? ')' : '0';
        /* 符号 */
        case 12: return shift ? '_' : '-';
        case 13: return shift ? '+' : '=';
        case 26: return shift ? '{' : '[';
        case 27: return shift ? '}' : ']';
        case 43: return shift ? '|' : '\\';
        case 39: return shift ? ':' : ';';
        case 40: return shift ? '"' : '\'';
        case 41: return shift ? '~' : '`';
        case 51: return shift ? '<' : ',';
        case 52: return shift ? '>' : '.';
        case 53: return shift ? '?' : '/';
        /* 空白/控制 */
        case 57: return ' ';       /* SPACE */
        case 28: return '\n';      /* ENTER */
        case 15: return '\t';      /* TAB */
        case 14: return '\b';      /* BACKSPACE */
        case 1:  return 27;        /* ESC */
        default: return 0;         /* 未映射 */
    }
}

/* 修饰键 make/break：KEY_LEFTSHIFT=42 RIGHTSHIFT=54 LEFTCTRL=29 RIGHTCTRL=97
 * LEFTALT=56 RIGHTALT=100 */
static int vin_mod_bit(uint16_t code, uint32_t *bit) {
    switch (code) {
        case 42: case 54:  *bit = VIN_MOD_SHIFT; return 1;
        case 29: case 97:  *bit = VIN_MOD_CTRL;  return 1;
        case 56: case 100: *bit = VIN_MOD_ALT;   return 1;
        default: return 0;
    }
}

/* ---- 把第 i 个事件 buffer 投递到 avail 环（write-only 单描述符） ---- */
static void vin_post_buffer(vinput_dev_t *v, uint16_t desc_idx) {
    virtqueue_t *vq = &v->vq;
    vq->desc[desc_idx].addr  = v->evt_phys + (uint64_t)desc_idx * EVT_SZ;
    vq->desc[desc_idx].len   = EVT_SZ;
    vq->desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;   /* device writes event */
    vq->desc[desc_idx].next  = 0;

    uint16_t ai = vq->avail->idx % vq->queue_size;
    vq->avail->ring[ai] = desc_idx;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx++;
    __asm__ volatile("" ::: "memory");
}

/* ---- 处理单条 evdev 事件 ---- */
static void vin_handle_event(vinput_dev_t *v, const virtio_input_event_t *e) {
    switch (e->type) {
        case VIRTIO_INPUT_EV_KEY: {
            uint32_t bit;
            if (vin_mod_bit(e->code, &bit)) {
                if (e->value)  v->mods |= bit;      /* press/repeat */
                else           v->mods &= ~bit;     /* release */
                return;
            }
            /* 鼠标按钮 */
            if (e->code == VIRTIO_INPUT_BTN_LEFT ||
                e->code == VIRTIO_INPUT_BTN_RIGHT ||
                e->code == VIRTIO_INPUT_BTN_MIDDLE) {
                uint32_t mbit = (e->code == VIRTIO_INPUT_BTN_LEFT)  ? 1u :
                                (e->code == VIRTIO_INPUT_BTN_RIGHT) ? 2u : 4u;
                if (e->value) v->buttons |= mbit;
                else          v->buttons &= ~mbit;
                return;
            }
            /* 普通键：仅在 press/repeat (value!=0) 时注入 */
            if (e->value) {
                int kc = vin_map_key(e->code, v->mods);
                if (kc) gui_post_key_code_with_modifiers(kc, v->mods);
            }
            return;
        }
        case VIRTIO_INPUT_EV_REL:
            if (e->code == VIRTIO_INPUT_REL_X)     v->acc_dx += (int32_t)e->value;
            else if (e->code == VIRTIO_INPUT_REL_Y) v->acc_dy += (int32_t)e->value;
            else if (e->code == VIRTIO_INPUT_REL_WHEEL) v->acc_wheel += (int32_t)e->value;
            return;
        case VIRTIO_INPUT_EV_ABS:
            if (e->code == VIRTIO_INPUT_ABS_X) { v->abs_x = (int32_t)e->value; v->have_abs = 1; }
            else if (e->code == VIRTIO_INPUT_ABS_Y) { v->abs_y = (int32_t)e->value; v->have_abs = 1; }
            return;
        case VIRTIO_INPUT_EV_SYN:
            /* 一帧结束：提交鼠标聚合 */
            if (v->have_abs) {
                mouse_set_absolute_position_with_wheel(v->abs_x, v->abs_y,
                                                       (uint8_t)v->buttons, v->acc_wheel);
                v->have_abs = 0;
            } else if (v->acc_dx || v->acc_dy || v->acc_wheel) {
                mouse_inject_relative(v->acc_dx, v->acc_dy, (uint8_t)v->buttons, v->acc_wheel);
            } else {
                /* 纯按钮变化也要提交一次（相对模式，零位移） */
                mouse_inject_relative(0, 0, (uint8_t)v->buttons, 0);
            }
            v->acc_dx = v->acc_dy = v->acc_wheel = 0;
            return;
        default:
            return;
    }
}

/* ---- 轮询单设备 eventq ---- */
static void vin_poll_dev(vinput_dev_t *v) {
    virtqueue_t *vq = &v->vq;
    while (vq->last_used != vq->used->idx) {
        __asm__ volatile("" ::: "memory");
        uint16_t ui = vq->last_used % vq->queue_size;
        uint16_t desc_idx = (uint16_t)vq->used->ring[ui].id;
        /* 读取 buffer 内事件 */
        virtio_input_event_t ev;
        const uint8_t *src = v->evt_buf + (uint64_t)desc_idx * EVT_SZ;
        __builtin_memcpy(&ev, src, EVT_SZ);
        vin_handle_event(v, &ev);
        /* 重新投递该 buffer */
        vin_post_buffer(v, desc_idx);
        vq->last_used++;
    }
    virtio_modern_notify(&v->dev, VINPUT_EVENTQ, v->notify_off);
}

void virtio_input_poll(void) {
    for (uint32_t i = 0; i < g_vin_count; i++)
        if (g_vin[i].active) vin_poll_dev(&g_vin[i]);
}

uint32_t virtio_input_device_count(void) { return g_vin_count; }

/* ---- 初始化单个设备 ---- */
static int vin_init_one(const pci_device_t *pci, vinput_dev_t *v) {
    if (virtio_modern_attach(pci, &v->dev) != 0) return -1;
    virtio_modern_reset(&v->dev);

    /* virtio-input 无强制特征位，接受设备提供的通用集（保留 VERSION_1） */
    uint64_t feats = virtio_modern_get_features(&v->dev);
    feats &= (1ull << 32);   /* 仅保留 VIRTIO_F_VERSION_1 (bit32) */
    if (virtio_modern_set_features(&v->dev, feats) != 0) return -1;

    if (virtio_modern_setup_queue(&v->dev, VINPUT_EVENTQ, &v->vq,
                                  &v->notify_off) != 0)
        return -1;

    /* 分配事件 buffer 页（VINPUT_BUF_COUNT * 8 字节，单页足够） */
    x86_64_phys_addr_t p = arch_x86_64_pmm_alloc_pages(1);
    if (!p) return -1;
    v->evt_phys = (uint64_t)p;
    v->evt_buf  = (uint8_t *)(uintptr_t)p;
    __builtin_memset(v->evt_buf, 0, VINPUT_BUF_COUNT * EVT_SZ);

    virtio_modern_set_driver_ok(&v->dev);

    /* 预投递所有事件 buffer */
    for (uint16_t d = 0; d < VINPUT_BUF_COUNT && d < v->vq.queue_size; d++)
        vin_post_buffer(v, d);
    __asm__ volatile("" ::: "memory");
    virtio_modern_notify(&v->dev, VINPUT_EVENTQ, v->notify_off);

    v->active = 1;
    return 0;
}

void virtio_input_init(void) {
    g_vin_count = 0;
    for (uint32_t i = 0; i < VINPUT_MAX_DEVS; i++) {
        const pci_device_t *pci =
            pci_find_nth_by_id(VIRTIO_INPUT_VENDOR, VIRTIO_INPUT_DEVICE, i);
        if (!pci) break;
        vinput_dev_t *v = &g_vin[g_vin_count];
        __builtin_memset(v, 0, sizeof(*v));
        if (vin_init_one(pci, v) == 0) {
            g_vin_count++;
            early_console64_write("[virtio-input] device up\n");
        } else {
            early_console64_write("[virtio-input] device init failed\n");
        }
    }
    if (g_vin_count == 0)
        early_console64_write("[virtio-input] no device (1af4:1052)\n");
}

/* GUI 平台输入轮询 hook 的强符号实现（覆盖 gui.c 弱符号）：
 * 每帧由 gui_poll_mouse() 调用，将 virtio-input eventq 事件翻译并注入。 */
void gui_platform_poll_input(void) {
    virtio_input_poll();
}
