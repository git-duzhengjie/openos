#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# 修复 USB MSC 缓冲区物理地址问题:
# g_cbw_buf/g_csw_buf/g_data_buf 是内核高半区静态数组(vaddr=0xFFFFFFFF8...),
# 直接把 vaddr 当 DMA 物理地址交给 xHCI 会指向错误物理页 → QEMU 读不到 TRB 数据。
# 改用 pmm 分配恒等映射的物理页(virt==phys), 与 HID/ring 缓冲一致。
f = 'src/arch/x86_64/gui64/usb_msc.c'
s = open(f, encoding='utf-8').read()

# 1) include pmm64.h
old_inc = '#include "usb_msc.h"\n#include "xhci64.h"\n#include "serial.h"'
new_inc = '#include "usb_msc.h"\n#include "xhci64.h"\n#include "serial.h"\n#include "pmm64.h"'
assert old_inc in s
s = s.replace(old_inc, new_inc, 1)

# 2) 静态数组 -> 指针 + 初始化标志
old_buf = '''/* ---- 传输用 DMA 缓冲（恒等映射物理地址）---- */
/* CBW/CSW 小缓冲 + 数据缓冲，静态对齐分配 */
static uint8_t g_cbw_buf[64] __attribute__((aligned(64)));
static uint8_t g_csw_buf[64] __attribute__((aligned(64)));
static uint8_t g_data_buf[512] __attribute__((aligned(64)));'''
new_buf = '''/* ---- 传输用 DMA 缓冲（恒等映射物理地址）---- */
/* 必须用 pmm 分配恒等映射物理页(virt==phys)，不能用内核高半区静态数组，
 * 否则把内核 vaddr(0xFFFFFFFF8...) 当 DMA 物理地址会让 xHCI 读到错误物理页。 */
static uint8_t *g_cbw_buf  = 0;   /* 64 B */
static uint8_t *g_csw_buf  = 0;   /* 64 B */
static uint8_t *g_data_buf = 0;   /* 512 B */

/* 分配 DMA 缓冲（一页足够放下 CBW+CSW+DATA），恒等映射物理地址 */
static int msc_dma_init(void) {
    if (g_cbw_buf) return 0;                 /* 已初始化 */
    uint64_t p = arch_x86_64_pmm_alloc_pages(1);
    if (!p) return -1;
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (int i = 0; i < 4096; i++) b[i] = 0;
    g_cbw_buf  = (uint8_t *)(uintptr_t)(p + 0);      /* [0..63]   */
    g_csw_buf  = (uint8_t *)(uintptr_t)(p + 64);     /* [64..127] */
    g_data_buf = (uint8_t *)(uintptr_t)(p + 128);    /* [128..639]*/
    return 0;
}'''
assert old_buf in s, 'buf block not found'
s = s.replace(old_buf, new_buf, 1)

open(f, 'w', encoding='utf-8').write(s)
print('patched msc dma buffers OK')
