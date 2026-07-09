/* ============================================================
 * openos —— 声卡 / 音频子系统（M2.4）
 *
 * sound.c —— 音频设备管理层：
 *   - PCI 探测 Multimedia(class 0x04) 设备：AC97 / HDA / 其他音频
 *   - PC Speaker（PIT channel 2 + 8255 门控）蜂鸣器
 *   - 设备表 / 统计信息 / lspci 风格打印
 *
 * AC97 PCM 播放实现见 ac97.c（sound_init 内接线）。
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include "../../../kernel/include/sound.h"
#include "../../../kernel/include/pci.h"
#include "../../../kernel/include/io.h"
#include "../../../kernel/include/serial.h"
#include "../include/delay64.h"

/* ---- 日志辅助 ---- */
static void slog(const char *s) { serial_write(s); }
static void slog_hex(uint32_t v) { serial_write_hex(v); }
static void slog_dec(uint32_t v) {
    char buf[12]; int i = 11; buf[11] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    else { while (v && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; } }
    serial_write(&buf[i]);
}
#define SLOG(s) serial_write("[sound] " s "\n")

/* ---- PC Speaker 端口 ---- */
#define PIT_CHANNEL2   0x42u
#define PIT_COMMAND    0x43u
#define SPEAKER_PORT   0x61u
#define PIT_FREQUENCY  1193182u   /* PIT 输入时钟 1.193182 MHz */

/* ---- 设备表 / 统计 ---- */
static sound_device_t g_devices[SOUND_MAX_DEVICES];
static sound_stats_t  g_stats;

/* AC97 驱动接口（ac97.c 提供）：探测到 AC97 时调用，成功返回 1 */
extern int ac97_probe(sound_device_t *dev);

/* ------------------------------------------------------------
 * PC Speaker 蜂鸣器
 * ---------------------------------------------------------- */
void sound_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    if (frequency_hz == 0) frequency_hz = 1000;
    if (frequency_hz < 20)  frequency_hz = 20;
    if (frequency_hz > 20000) frequency_hz = 20000;

    uint32_t divisor = PIT_FREQUENCY / frequency_hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor == 0) divisor = 1;

    /* PIT channel 2, mode 3（方波），先写低字节再写高字节 */
    outb(PIT_COMMAND, 0xB6u);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF));

    /* 打开门控（bit0=定时器2门控, bit1=扬声器数据使能） */
    uint8_t tmp = inb(SPEAKER_PORT);
    if ((tmp & 0x03u) != 0x03u) {
        outb(SPEAKER_PORT, tmp | 0x03u);
    }

    /* 持续 duration_ms */
    if (duration_ms == 0) duration_ms = 100;
    arch_x86_64_delay_ms(duration_ms);

    /* 关闭扬声器 */
    tmp = inb(SPEAKER_PORT) & 0xFCu;
    outb(SPEAKER_PORT, tmp);

    g_stats.beep_count++;
    /* 记入 PC Speaker 设备的 beep_count */
    for (uint32_t i = 0; i < SOUND_MAX_DEVICES; i++) {
        if (g_devices[i].used && g_devices[i].type == SOUND_DEVICE_PC_SPEAKER) {
            g_devices[i].beep_count++;
            break;
        }
    }
}

/* ------------------------------------------------------------
 * 设备表管理
 * ---------------------------------------------------------- */
static sound_device_t *sound_alloc_slot(void) {
    for (uint32_t i = 0; i < SOUND_MAX_DEVICES; i++) {
        if (!g_devices[i].used) return &g_devices[i];
    }
    return NULL;
}

static sound_device_type_t sound_classify(uint8_t subclass,
                                          uint16_t vendor, uint16_t device) {
    if (subclass == SOUND_PCI_SUBCLASS_HDA) return SOUND_DEVICE_HDA;
    if (subclass == SOUND_PCI_SUBCLASS_AUDIO) {
        /* Intel 82801AA AC97 = 8086:2415 系列 */
        if (vendor == 0x8086u &&
            (device == 0x2415u || device == 0x2425u || device == 0x2445u ||
             device == 0x2485u || device == 0x24C5u || device == 0x24D5u)) {
            return SOUND_DEVICE_AC97;
        }
        return SOUND_DEVICE_AC97; /* 多数 subclass 0x01 为 AC97 兼容 */
    }
    (void)vendor; (void)device;
    return SOUND_DEVICE_AUDIO;
}

/* 登记一个 PCI 音频设备 */
static void sound_register_pci(const pci_device_t *pd) {
    sound_device_t *sd = sound_alloc_slot();
    if (!sd) { SLOG("device table full"); return; }

    sd->used       = 1;
    sd->bus        = pd->bus;
    sd->dev        = pd->dev;
    sd->func       = pd->func;
    sd->irq        = pd->irq_line;
    sd->vendor_id  = pd->vendor_id;
    sd->device_id  = pd->device_id;
    sd->pci_class  = ((uint32_t)pd->class_code << 8) | pd->subclass;
    sd->type       = sound_classify(pd->subclass, pd->vendor_id, pd->device_id);
    sd->playback_ready = 0;
    sd->beep_count = 0;

    /* 解析 BAR：IO 端口 → io_base；MMIO → mem_base */
    sd->io_base  = 0;
    sd->mem_base = 0;
    for (int b = 0; b < 6; b++) {
        if (pd->bars[b].size == 0) continue;
        if (pd->bars[b].is_mmio) {
            if (sd->mem_base == 0)
                sd->mem_base = (uint32_t)pd->bars[b].base;
        } else {
            if (sd->io_base == 0)
                sd->io_base = (uint16_t)pd->bars[b].base;
        }
    }

    slog("[sound] found ");
    slog(sound_device_type_name(sd->type));
    slog(" "); slog_hex(sd->vendor_id);
    slog(":"); slog_hex(sd->device_id);
    slog(" io="); slog_hex(sd->io_base);
    slog(" mem="); slog_hex(sd->mem_base);
    slog(" irq="); slog_dec(sd->irq);
    slog("\n");

    switch (sd->type) {
        case SOUND_DEVICE_AC97: g_stats.ac97_count++; break;
        case SOUND_DEVICE_HDA:  g_stats.hda_count++;  break;
        default:                g_stats.audio_count++; break;
    }
    g_stats.device_count++;

    /* AC97 设备：调用 ac97 驱动初始化 + PCM 播放自检 */
    if (sd->type == SOUND_DEVICE_AC97) {
        if (ac97_probe(sd)) {
            sd->playback_ready = 1;
            SLOG("AC97 playback ready");
        } else {
            SLOG("AC97 probe failed");
        }
    }
}

/* 登记 PC Speaker（虚拟设备，总是存在） */
static void sound_register_speaker(void) {
    sound_device_t *sd = sound_alloc_slot();
    if (!sd) return;
    sd->used = 1;
    sd->type = SOUND_DEVICE_PC_SPEAKER;
    sd->vendor_id = 0;
    sd->device_id = 0;
    sd->io_base = SPEAKER_PORT;
    sd->playback_ready = 1;
    g_stats.pc_speaker_count++;
    g_stats.device_count++;
    SLOG("PC Speaker registered");
}

/* ------------------------------------------------------------
 * 扫描 / 初始化
 * ---------------------------------------------------------- */
void sound_rescan(void) {
    /* 清空设备表与统计 */
    for (uint32_t i = 0; i < SOUND_MAX_DEVICES; i++) g_devices[i].used = 0;
    uint32_t saved_beeps = g_stats.beep_count;
    for (uint32_t i = 0; i < sizeof(g_stats) / 4; i++) ((uint32_t *)&g_stats)[i] = 0;
    g_stats.beep_count = saved_beeps;
    g_stats.scans++;

    /* 遍历所有已知 PCI 设备，挑出 Multimedia(0x04) */
    uint32_t n = pci_known_device_count();
    for (uint32_t i = 0; i < n; i++) {
        const pci_device_t *pd = pci_get_device(i);
        if (!pd) continue;
        if (pd->class_code != SOUND_PCI_CLASS_MULTIMEDIA) continue;
        /* 只登记音频类 subclass（0x01 audio / 0x03 HDA / 0x80 other） */
        if (pd->subclass == SOUND_PCI_SUBCLASS_VIDEO) continue;
        sound_register_pci(pd);
    }

    /* 始终登记 PC Speaker 作为保底发声设备 */
    sound_register_speaker();
}

void sound_init(void) {
    SLOG("init: scanning multimedia devices...");
    for (uint32_t i = 0; i < SOUND_MAX_DEVICES; i++) g_devices[i].used = 0;
    for (uint32_t i = 0; i < sizeof(g_stats) / 4; i++) ((uint32_t *)&g_stats)[i] = 0;
    sound_rescan();

    slog("[sound] init done: ");
    slog_dec(g_stats.device_count); slog(" device(s), ac97=");
    slog_dec(g_stats.ac97_count); slog(" hda=");
    slog_dec(g_stats.hda_count); slog("\n");
}

/* ------------------------------------------------------------
 * 访问器 / 打印
 * ---------------------------------------------------------- */
uint32_t sound_device_count(void) { return g_stats.device_count; }

const sound_device_t *sound_get_device(uint32_t index) {
    if (index >= SOUND_MAX_DEVICES) return NULL;
    if (!g_devices[index].used) return NULL;
    return &g_devices[index];
}

const sound_stats_t *sound_get_stats(void) { return &g_stats; }

const char *sound_device_type_name(sound_device_type_t type) {
    switch (type) {
        case SOUND_DEVICE_AC97:       return "AC97";
        case SOUND_DEVICE_HDA:        return "HD-Audio";
        case SOUND_DEVICE_AUDIO:      return "Audio";
        case SOUND_DEVICE_PC_SPEAKER: return "PC-Speaker";
        default:                      return "Unknown";
    }
}

void sound_print_info(void) {
    slog("===== sound devices =====\n");
    for (uint32_t i = 0; i < SOUND_MAX_DEVICES; i++) {
        if (!g_devices[i].used) continue;
        const sound_device_t *d = &g_devices[i];
        slog("  ["); slog_dec(i); slog("] ");
        slog(sound_device_type_name(d->type));
        if (d->type != SOUND_DEVICE_PC_SPEAKER) {
            slog(" "); slog_hex(d->vendor_id);
            slog(":"); slog_hex(d->device_id);
            slog(" "); slog_dec(d->bus); slog(":");
            slog_dec(d->dev); slog("."); slog_dec(d->func);
            slog(" io="); slog_hex(d->io_base);
            slog(" mem="); slog_hex(d->mem_base);
        }
        slog(d->playback_ready ? " [ready]" : " [--]");
        slog("\n");
    }
    slog("  scans="); slog_dec(g_stats.scans);
    slog(" total="); slog_dec(g_stats.device_count);
    slog(" beeps="); slog_dec(g_stats.beep_count);
    slog("\n=========================\n");
}
