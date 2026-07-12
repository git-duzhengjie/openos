/* =====================================================================
 * virtio 1.0 modern PCI transport implementation
 *
 * Locates the four MMIO capability windows (common/notify/isr/device) of
 * a modern virtio PCI device, maps them into kernel virtual space and
 * drives the status handshake + split-virtqueue setup per virtio 1.0.
 *
 * Dependencies:
 *   - pci64        capability walk + config space reads
 *   - vmm64        MMIO window mapping (arch_x86_64_vmm_map_range)
 *   - pmm64        contiguous phys pages for virtqueue rings (identity low)
 * ===================================================================== */

#include "virtio_modern.h"
#include "vmm64.h"
#include "pmm64.h"
#include "serial.h"

/* ---- small local helpers ---- */

static void vm_memset(void *d, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)d;
    while (n--) *p++ = (uint8_t)c;
}

/* Map a BAR MMIO window (identity: kernel virt == phys) so the region is
 * present + uncacheable.  virtio register windows must not be cached. */
static volatile uint8_t *map_bar_window(const pci_device_t *pci, uint8_t bar,
                                        uint32_t offset, uint32_t length) {
    if (bar >= 6) return 0;
    const pci_bar_t *b = &pci->bars[bar];
    if (!b->is_mmio || b->size == 0) return 0;
    if ((uint64_t)offset + length > b->size) return 0;

    uint64_t phys = b->base + offset;
    uint64_t page = phys & ~0xFFFULL;
    uint64_t end  = (phys + length + 0xFFFULL) & ~0xFFFULL;
    uint64_t span = end - page;

    if (arch_x86_64_vmm_map_range((x86_64_virt_addr_t)page,
                                  (x86_64_phys_addr_t)page,
                                  (x86_64_size_t)span,
                                  OPENOS_X86_64_VMM_MMIO_FLAGS) != 0) {
        return 0;
    }
    return (volatile uint8_t *)phys;
}

/* Walk the PCI capability list, find each virtio cap and map its window. */
int virtio_modern_attach(const pci_device_t *pci, virtio_modern_dev_t *out) {
    if (!pci || !out) return -1;
    vm_memset(out, 0, sizeof(*out));
    out->pci = *pci;

    /* status register bit 4 => capability list present */
    uint16_t status = pci_read16(pci->bus, pci->dev, pci->func, 0x06);
    if (!(status & (1u << 4))) {
        serial_write("[virtio-modern] no PCI cap list\n");
        return -2;
    }

    uint8_t cap = pci_read8(pci->bus, pci->dev, pci->func, 0x34) & 0xFC;
    int guard = 0;
    while (cap != 0 && guard++ < 48) {
        uint8_t vndr = pci_read8(pci->bus, pci->dev, pci->func, cap);
        uint8_t next = pci_read8(pci->bus, pci->dev, pci->func, cap + 1);
        if (vndr == VIRTIO_PCI_CAP_VENDOR_ID) {
            uint8_t cfg_type = pci_read8(pci->bus, pci->dev, pci->func, cap + 3);
            uint8_t bar      = pci_read8(pci->bus, pci->dev, pci->func, cap + 4);
            uint32_t offset  = pci_read32(pci->bus, pci->dev, pci->func, cap + 8);
            uint32_t length  = pci_read32(pci->bus, pci->dev, pci->func, cap + 12);

            switch (cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                out->common = (volatile virtio_pci_common_cfg_t *)
                    map_bar_window(pci, bar, offset, length);
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG: {
                out->notify_base = map_bar_window(pci, bar, offset, length);
                /* notify cap has an extra u32 multiplier at cap+16 */
                out->notify_off_multiplier =
                    pci_read32(pci->bus, pci->dev, pci->func, cap + 16);
                break;
            }
            case VIRTIO_PCI_CAP_ISR_CFG:
                out->isr = map_bar_window(pci, bar, offset, length);
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                out->device_cfg = map_bar_window(pci, bar, offset, length);
                out->device_cfg_len = length;
                break;
            default:
                break;
            }
        }
        cap = next & 0xFC;
    }

    if (!out->common || !out->notify_base) {
        serial_write("[virtio-modern] missing common/notify cap\n");
        return -3;
    }

    /* enable MMIO decode + bus master in PCI command register */
    uint16_t cmd = pci_read16(pci->bus, pci->dev, pci->func, 0x04);
    cmd |= (1u << 1) | (1u << 2); /* Memory Space + Bus Master */
    pci_write16(pci->bus, pci->dev, pci->func, 0x04, cmd);

    out->ready = 1;
    return 0;
}

/* ---- status handshake ---- */

void virtio_modern_reset(virtio_modern_dev_t *d) {
    if (!d || !d->common) return;
    d->common->device_status = 0;               /* reset */
    while (d->common->device_status != 0) { }    /* wait for reset ack */
    d->common->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    d->common->device_status =
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
}

uint64_t virtio_modern_get_features(virtio_modern_dev_t *d) {
    if (!d || !d->common) return 0;
    d->common->device_feature_select = 0;
    uint64_t lo = d->common->device_feature;
    d->common->device_feature_select = 1;
    uint64_t hi = d->common->device_feature;
    return (hi << 32) | lo;
}

int virtio_modern_set_features(virtio_modern_dev_t *d, uint64_t features) {
    if (!d || !d->common) return -1;
    d->common->driver_feature_select = 0;
    d->common->driver_feature = (uint32_t)(features & 0xFFFFFFFFu);
    d->common->driver_feature_select = 1;
    d->common->driver_feature = (uint32_t)(features >> 32);

    d->common->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(d->common->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        return -2; /* device rejected the negotiated feature set */
    }
    return 0;
}

void virtio_modern_set_driver_ok(virtio_modern_dev_t *d) {
    if (!d || !d->common) return;
    d->common->device_status |= VIRTIO_STATUS_DRIVER_OK;
}

void virtio_modern_fail(virtio_modern_dev_t *d) {
    if (!d || !d->common) return;
    d->common->device_status |= VIRTIO_STATUS_FAILED;
}

/* ---- split virtqueue setup ---- */

/* Total bytes needed for a split ring of qs descriptors (desc + avail +
 * used, each aligned as required).  Layout is contiguous for simplicity;
 * the modern transport lets us program the three phys addresses freely. */
static uint32_t vq_ring_bytes(uint16_t qs, uint32_t *avail_off_out,
                              uint32_t *used_off_out) {
    uint32_t desc_bytes  = 16u * qs;
    uint32_t avail_bytes = 2u + 2u + 2u * qs + 2u; /* flags,idx,ring,uev */
    uint32_t avail_off   = desc_bytes;
    uint32_t used_off = (desc_bytes + avail_bytes + (VIRTIO_PCI_VRING_ALIGN - 1))
                        & ~(VIRTIO_PCI_VRING_ALIGN - 1);
    uint32_t used_bytes  = 2u + 2u + 8u * qs + 2u;
    if (avail_off_out) *avail_off_out = avail_off;
    if (used_off_out)  *used_off_out  = used_off;
    return used_off + used_bytes;
}

int virtio_modern_setup_queue(virtio_modern_dev_t *d, uint16_t index,
                              virtqueue_t *vq, uint16_t *notify_off_out) {
    if (!d || !d->common || !vq) return -1;

    d->common->queue_select = index;
    uint16_t qs = d->common->queue_size;
    if (qs == 0) {
        serial_write("[virtio-modern] queue size=0 (absent)\n");
        return -2;
    }

    uint32_t avail_off = 0, used_off = 0;
    uint32_t bytes = vq_ring_bytes(qs, &avail_off, &used_off);
    uint32_t pages = (bytes + 4095u) / 4096u;
    x86_64_phys_addr_t phys = arch_x86_64_pmm_alloc_pages(pages);
    if (phys == 0) {
        serial_write("[virtio-modern] vq alloc failed\n");
        return -3;
    }
    /* identity mapped low memory: phys usable directly as virt */
    uint8_t *base = (uint8_t *)(uintptr_t)phys;
    vm_memset(base, 0, pages * 4096u);

    vq->queue_size  = qs;
    vq->queue_index = index;
    vq->desc  = (volatile virtq_desc_t  *)(base);
    vq->avail = (volatile virtq_avail_t *)(base + avail_off);
    vq->used  = (volatile virtq_used_t  *)(base + used_off);
    vq->ring_phys  = phys;
    vq->ring_bytes = pages * 4096u;
    vq->last_used  = 0;

    for (uint16_t i = 0; i < qs; i++) {
        vq->desc[i].next = (uint16_t)(i + 1);
    }
    vq->free_head = 0;
    vq->num_free  = qs;

    /* program the three ring phys addresses + enable */
    d->common->queue_select = index;
    d->common->queue_desc   = phys;
    d->common->queue_driver = phys + avail_off;
    d->common->queue_device = phys + used_off;
    uint16_t noff = d->common->queue_notify_off;
    d->common->queue_enable = 1;

    if (notify_off_out) *notify_off_out = noff;
    return 0;
}

void virtio_modern_notify(virtio_modern_dev_t *d, uint16_t index,
                          uint16_t notify_off) {
    if (!d || !d->notify_base) return;
    /* notify address = notify_base + notify_off * notify_off_multiplier */
    volatile uint16_t *reg = (volatile uint16_t *)
        (d->notify_base + (uint32_t)notify_off * d->notify_off_multiplier);
    *reg = index;
}

/* ---- device-specific config window ---- */

void virtio_modern_cfg_read(virtio_modern_dev_t *d, uint32_t off,
                            void *buf, uint32_t len) {
    if (!d || !d->device_cfg || !buf) return;
    if (off + len > d->device_cfg_len) return;
    volatile uint8_t *src = d->device_cfg + off;
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
}

void virtio_modern_cfg_write(virtio_modern_dev_t *d, uint32_t off,
                             const void *buf, uint32_t len) {
    if (!d || !d->device_cfg || !buf) return;
    if (off + len > d->device_cfg_len) return;
    volatile uint8_t *dst = d->device_cfg + off;
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
}
