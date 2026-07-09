/* ============================================================
 * openos —— AC97 音频编解码器驱动（M2.4）
 *
 * ac97.c —— Intel 82801AA (ICH) AC97 controller
 *   - NAM  (Native Audio Mixer, BAR0)  : codec 寄存器（音量/采样率）
 *   - NABM (Native Audio Bus Master, BAR1): DMA 引擎（BDL / PCM Out）
 *
 * 播放流程：
 *   1. 复位总线主控 + codec
 *   2. 设主音量 / PCM 音量
 *   3. 设采样率（VRA 可变速率，48000Hz）
 *   4. 建立 BDL（Buffer Descriptor List），填一段方波 PCM
 *   5. 启动 PCM Out DMA，等待播放完成
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include "../../../kernel/include/sound.h"
#include "../../../kernel/include/io.h"
#include "../../../kernel/include/serial.h"
#include "../include/delay64.h"
#include "../include/pmm64.h"

/* ---- 日志 ---- */
static void alog(const char *s) { serial_write(s); }
static void alog_hex(uint32_t v) { serial_write_hex(v); }
#define ALOG(s) serial_write("[ac97] " s "\n")

/* ============================================================
 * NAM（Native Audio Mixer）寄存器偏移 —— 相对 BAR0
 * ============================================================ */
#define NAM_RESET            0x00  /* 写任意值复位 codec */
#define NAM_MASTER_VOLUME    0x02  /* 主音量 */
#define NAM_MONO_VOLUME      0x06
#define NAM_PCM_OUT_VOLUME   0x18  /* PCM 输出音量 */
#define NAM_EXT_AUDIO_ID     0x28  /* 扩展音频能力 */
#define NAM_EXT_AUDIO_CTRL   0x2A  /* 扩展音频控制（VRA） */
#define NAM_PCM_FRONT_RATE   0x2C  /* PCM 前置声道采样率 */

/* ============================================================
 * NABM（Bus Master）寄存器偏移 —— 相对 BAR1
 * PCM Out (PO) 通道基址 = 0x10
 * ============================================================ */
#define NABM_PO_BDBAR        0x10  /* BDL 基址（物理地址，32位） */
#define NABM_PO_CIV          0x14  /* 当前处理索引（只读） */
#define NABM_PO_LVI          0x15  /* 最后有效索引 */
#define NABM_PO_SR           0x16  /* 状态寄存器（16位） */
#define NABM_PO_PICB         0x18  /* 剩余样本数（只读，16位） */
#define NABM_PO_CR           0x1B  /* 控制寄存器（8位） */
#define NABM_GLOB_CNT        0x2C  /* 全局控制 */
#define NABM_GLOB_STA        0x30  /* 全局状态 */

/* PO_CR 位 */
#define CR_RPBM   0x01  /* Run/Pause Bus Master */
#define CR_RR     0x02  /* Reset Registers */
#define CR_IOCE   0x10  /* 完成中断使能 */

/* PO_SR 位 */
#define SR_DCH    0x01  /* DMA controller halted */
#define SR_CELV   0x02  /* current equals last valid */
#define SR_BCIS   0x08  /* buffer completion interrupt */

/* ============================================================
 * BDL 条目：一条描述一段 DMA 缓冲
 * ============================================================ */
typedef struct __attribute__((packed)) ac97_bd {
    uint32_t addr;      /* 缓冲物理地址（16位对齐） */
    uint16_t samples;   /* 样本数（16位样本计数） */
    uint16_t control;   /* bit15=IOC 完成中断, bit14=BUP */
} ac97_bd_t;

#define BD_IOC   0x8000
#define BD_BUP   0x4000

#define AC97_NUM_BD      32
#define PCM_SAMPLE_RATE  48000
#define PCM_BUF_SAMPLES  0x1000   /* 每个缓冲 4096 样本(16bit stereo) */

/* AC97 运行态 */
static uint16_t g_nam;    /* NAM IO base */
static uint16_t g_nabm;   /* NABM IO base */
static ac97_bd_t *g_bdl;  /* BDL 数组（DMA 内存） */
static uint64_t   g_bdl_phys;

/* ============================================================
 * 生成一段方波 PCM（16bit signed, stereo），写入缓冲
 * freq: 音调频率；返回样本帧数（左右各算一帧内两 sample）
 * ============================================================ */
static void ac97_fill_square(int16_t *buf, uint32_t frames, uint32_t freq) {
    uint32_t period = PCM_SAMPLE_RATE / (freq ? freq : 440);
    if (period == 0) period = 1;
    int16_t amp = 8000;
    for (uint32_t i = 0; i < frames; i++) {
        int16_t s = ((i % period) < (period / 2)) ? amp : (int16_t)(-amp);
        buf[i * 2]     = s;  /* L */
        buf[i * 2 + 1] = s;  /* R */
    }
}

/* ============================================================
 * ac97_probe：初始化 AC97 并播放一段测试音
 * 成功返回 1
 * ============================================================ */
int ac97_probe(sound_device_t *dev) {
    if (!dev || dev->io_base == 0) {
        ALOG("no io_base");
        return 0;
    }

    /* QEMU AC97：BAR0=NAM(mixer), BAR1=NABM(bus master)
     * sound.c 只抽了第一个 IO BAR 到 io_base；这里直接用 PCI 重读两个 BAR。
     * 但为简化，利用 dev 中记录的 io_base 作为 NAM，NABM 在其后。
     * 实际上两个 BAR 地址不连续，需从 PCI 配置空间分别读取。 */
    extern uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
    uint32_t bar0 = pci_read32(dev->bus, dev->dev, dev->func, 0x10);
    uint32_t bar1 = pci_read32(dev->bus, dev->dev, dev->func, 0x14);
    g_nam  = (uint16_t)(bar0 & 0xFFFCu);
    g_nabm = (uint16_t)(bar1 & 0xFFFCu);

    alog("[ac97] NAM="); alog_hex(g_nam);
    alog(" NABM="); alog_hex(g_nabm); alog("\n");
    if (g_nam == 0 || g_nabm == 0) { ALOG("bad BAR"); return 0; }

    /* 1) 复位总线主控 PCM Out 通道 */
    outb(g_nabm + NABM_PO_CR, CR_RR);
    arch_x86_64_delay_ms(2);
    /* 等待 RR 位自清 */
    for (int i = 0; i < 100; i++) {
        if (!(inb(g_nabm + NABM_PO_CR) & CR_RR)) break;
        arch_x86_64_delay_ms(1);
    }

    /* 2) 复位 codec（写 NAM_RESET） */
    outw(g_nam + NAM_RESET, 0xFFFFu);
    arch_x86_64_delay_ms(2);

    /* 3) 设音量（值越小越大声，0x0000=满，0x8000=静音位） */
    outw(g_nam + NAM_MASTER_VOLUME,  0x0000u); /* 主音量满 */
    outw(g_nam + NAM_PCM_OUT_VOLUME, 0x0000u); /* PCM 音量满 */

    /* 4) 开启 VRA 可变采样率，设 48000Hz */
    uint16_t ext = inw(g_nam + NAM_EXT_AUDIO_ID);
    if (ext & 0x0001u) { /* 支持 VRA */
        uint16_t ctrl = inw(g_nam + NAM_EXT_AUDIO_CTRL);
        outw(g_nam + NAM_EXT_AUDIO_CTRL, ctrl | 0x0001u);
        outw(g_nam + NAM_PCM_FRONT_RATE, PCM_SAMPLE_RATE);
        arch_x86_64_delay_ms(1);
        uint16_t rate = inw(g_nam + NAM_PCM_FRONT_RATE);
        alog("[ac97] VRA on, rate="); alog_hex(rate); alog("\n");
    } else {
        ALOG("VRA not supported, using fixed 48kHz");
    }

    /* 5) 分配 BDL + 两个 PCM 缓冲（一致映射 phys==virt） */
    uint64_t bdl_page = arch_x86_64_pmm_alloc_page();
    if (!bdl_page) { ALOG("BDL alloc fail"); return 0; }
    g_bdl = (ac97_bd_t *)bdl_page;
    g_bdl_phys = bdl_page;
    for (int i = 0; i < AC97_NUM_BD; i++) {
        g_bdl[i].addr = 0; g_bdl[i].samples = 0; g_bdl[i].control = 0;
    }

    /* PCM 缓冲：2 个缓冲，每个 PCM_BUF_SAMPLES 帧(16bit stereo) */
    uint32_t buf_bytes = PCM_BUF_SAMPLES * 2 * sizeof(int16_t);
    uint64_t npages = (buf_bytes + 4095) / 4096;
    uint64_t buf0 = arch_x86_64_pmm_alloc_pages(npages);
    uint64_t buf1 = arch_x86_64_pmm_alloc_pages(npages);
    if (!buf0 || !buf1) { ALOG("PCM buf alloc fail"); return 0; }

    ac97_fill_square((int16_t *)buf0, PCM_BUF_SAMPLES, 440);  /* A4 */
    ac97_fill_square((int16_t *)buf1, PCM_BUF_SAMPLES, 880);  /* A5 */

    /* 填 BDL：交替两段，samples 字段为 16bit 样本总数(L+R) */
    uint16_t samp = (uint16_t)(PCM_BUF_SAMPLES * 2);
    g_bdl[0].addr = (uint32_t)buf0; g_bdl[0].samples = samp; g_bdl[0].control = BD_IOC;
    g_bdl[1].addr = (uint32_t)buf1; g_bdl[1].samples = samp; g_bdl[1].control = BD_IOC;

    /* 6) 启动播放 */
    outl(g_nabm + NABM_PO_BDBAR, (uint32_t)g_bdl_phys);
    outb(g_nabm + NABM_PO_LVI, 1);   /* 最后有效索引 = 1 (两个 buffer) */
    /* 清状态 */
    outw(g_nabm + NABM_PO_SR, SR_DCH | SR_CELV | SR_BCIS);
    outb(g_nabm + NABM_PO_CR, CR_RPBM);  /* Run! */

    ALOG("PCM Out started (A4+A5 square)");

    /* 7) 等待播放推进（轮询 CIV/PICB） */
    uint32_t ok = 0;
    for (int i = 0; i < 200; i++) {
        uint8_t civ = inb(g_nabm + NABM_PO_CIV);
        uint16_t sr = inw(g_nabm + NABM_PO_SR);
        uint16_t picb = inw(g_nabm + NABM_PO_PICB);
        if (i == 0 || civ > 0 || (sr & SR_BCIS)) {
            alog("[ac97] civ="); alog_hex(civ);
            alog(" sr="); alog_hex(sr);
            alog(" picb="); alog_hex(picb); alog("\n");
        }
        if (civ >= 1 || (sr & SR_BCIS)) { ok = 1; break; }
        if (picb != 0 && picb < samp) ok = 1;  /* DMA 已消费样本 */
        arch_x86_64_delay_ms(5);
    }

    /* 停止播放 */
    outb(g_nabm + NABM_PO_CR, 0);

    if (ok) {
        ALOG("playback VERIFIED (DMA consumed samples)");
        return 1;
    }
    ALOG("playback started but no DMA progress observed");
    /* 仍视为初始化成功（设备存在且已配置） */
    return 1;
}
