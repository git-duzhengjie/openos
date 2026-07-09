#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# 修复 xhci_wait_event: 遇到非目标 Transfer Event(HID中断) 时,
# 不再 return -1 放弃, 而是消费并分发该 HID event(拷入 rpt 缓冲 + re-arm),
# 然后继续 spin 等待自己的目标 event。彻底解决 MSC bulk 与 HID 中断的事件竞争。
import io

f = 'src/arch/x86_64/gui64/xhci64.c'
s = open(f, encoding='utf-8').read()

# ---- 1) 在 xhci_wait_event 之前插入 HID 分发 helper ----
anchor = 'static int xhci_wait_event(uint32_t want_type, uint64_t trb_phys,'
helper = '''/* 将一个已确认为 Transfer Event 的 TRB 分发给对应 HID 设备：
 * 命中某设备的 Interrupt-IN 端点 → 拷贝 report 到 rpt 缓冲 + 置 ready + re-arm。
 * 供 xhci_wait_event 在等待其它端点(如 MSC bulk)时顺带消费 HID 事件，避免丢报文。 */
static void xhci_dispatch_hid_event(volatile xhci_trb_t *evt) {
    uint32_t c     = evt->control;
    uint32_t eslot = TRB_GET_SLOT(evt);
    uint32_t eep   = (c >> 16) & 0x1F;
    int      resid = (int)(evt->status & 0xFFFFFF);

    int di = xhci_dev_by_slot(eslot);
    if (di < 0) return;
    xhci_dev_t *d = &g_devs[di];
    if (!d->ep_in_ring) return;
    uint32_t want_dci = (d->ep_in_addr & 0x0F) * 2 + 1;
    if (eep != want_dci) return;               /* 非该设备 Interrupt-IN */

    uint32_t rlen = d->ep_in_mps ? d->ep_in_mps : 8;
    if (rlen > sizeof(d->rpt_data)) rlen = sizeof(d->rpt_data);
    uint32_t n = (rlen > (uint32_t)resid) ? (rlen - (uint32_t)resid) : 0;
    if (n > sizeof(d->rpt_data)) n = sizeof(d->rpt_data);
    for (uint32_t i = 0; i < n; i++) d->rpt_data[i] = d->hid_buf[i];
    d->rpt_len   = n;
    d->rpt_ready = 1;
    xhci_hid_arm(d);                           /* 重新武装下一个中断传输 */
}

'''
assert anchor in s, 'wait_event anchor not found'
s = s.replace(anchor, helper + anchor, 1)

# ---- 2) 修改 wait_event 里非目标 Transfer Event 的处理 ----
old_branch = '''            } else if (type == TRB_TRANSFER_EVT) {
                /* 非目标的 Transfer Event（HID Interrupt-IN 完成）：
                 * 不能当作"杂项"消费丢弃，否则 HID report 永久丢失。
                 * 交给 xhci_pump_events 分发。这里遇到即停，保留在环上。 */
                return -1;
            }'''
new_branch = '''            } else if (type == TRB_TRANSFER_EVT) {
                /* 非目标的 Transfer Event（通常是 HID Interrupt-IN 完成）：
                 * 不能放弃返回(否则与本次等待的端点事件竞争会互相卡死)，
                 * 也不能丢弃(否则 HID report 永久丢失)。
                 * 就地分发给对应 HID 设备(拷 report + re-arm)，然后消费该 TRB
                 * 继续 spin 等待本次真正的目标 event。 */
                xhci_dispatch_hid_event(evt);
            }'''
assert old_branch in s, 'wait_event branch not found'
s = s.replace(old_branch, new_branch, 1)

open(f, 'w', encoding='utf-8').write(s)
print('patched wait_event OK')
