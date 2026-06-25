/* ============================================================
 * openos - shared VirtIO PCI legacy transport helpers
 * ============================================================ */
#include "../include/virtio.h"
#include "../include/io.h"
#include "../include/string.h"

void virtio_mb(void) {
    __asm__ volatile ("" ::: "memory");
}

uint32_t virtio_ptr32(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

uint64_t virtio_io_read64(uint16_t port) {
    uint32_t lo = inl(port);
    uint32_t hi = inl((uint16_t)(port + 4u));
    return ((uint64_t)hi << 32) | lo;
}

uint32_t virtio_pci_bar_base(uint8_t bus, uint8_t dev, uint8_t func,
                             uint8_t bar_index, uint8_t want_io) {
    uint32_t bar = pci_read32(bus, dev, func, (uint8_t)(PCI_OFFSET_BAR0 + (bar_index * 4u)));
    if (bar == 0u || bar == 0xFFFFFFFFu) return 0u;
    if ((bar & 0x1u) != 0u) return want_io ? (bar & ~0x3u) : 0u;
    return want_io ? 0u : (bar & ~0xFu);
}

int virtio_pci_is_modern_device(uint16_t vendor, uint16_t device,
                                uint16_t modern_device_id) {
    uint16_t modern_id;
    if (vendor != VIRTIO_PCI_VENDOR_ID ||
        device < VIRTIO_MODERN_DEVICE_MIN ||
        device > VIRTIO_MODERN_DEVICE_MAX) {
        return 0;
    }
    modern_id = (uint16_t)(device - VIRTIO_MODERN_DEVICE_MIN);
    return modern_id == modern_device_id;
}

void virtio_pci_reset_legacy(uint32_t io_base) {
    outb((uint16_t)(io_base + VIRTIO_PCI_STATUS), 0u);
}

void virtio_pci_set_status_legacy(uint32_t io_base, uint8_t status) {
    outb((uint16_t)(io_base + VIRTIO_PCI_STATUS), status);
}

uint8_t virtio_pci_get_status_legacy(uint32_t io_base) {
    return inb((uint16_t)(io_base + VIRTIO_PCI_STATUS));
}

uint32_t virtio_pci_read_features_legacy(uint32_t io_base) {
    return inl((uint16_t)(io_base + VIRTIO_PCI_HOST_FEATURES));
}

void virtio_pci_write_features_legacy(uint32_t io_base, uint32_t features) {
    outl((uint16_t)(io_base + VIRTIO_PCI_GUEST_FEATURES), features);
}

uint32_t virtio_pci_config_read32_legacy(uint32_t io_base, uint16_t offset) {
    return inl((uint16_t)(io_base + VIRTIO_PCI_CONFIG + offset));
}

uint64_t virtio_pci_config_read64_legacy(uint32_t io_base, uint16_t offset) {
    return virtio_io_read64((uint16_t)(io_base + VIRTIO_PCI_CONFIG + offset));
}

int virtio_pci_setup_queue_legacy(uint32_t io_base, uint16_t queue_index,
                                  void *queue, uint16_t min_queue_size,
                                  uint16_t *host_queue_size) {
    uint16_t queue_num;
    outw((uint16_t)(io_base + VIRTIO_PCI_QUEUE_SEL), queue_index);
    queue_num = inw((uint16_t)(io_base + VIRTIO_PCI_QUEUE_NUM));
    if (host_queue_size) *host_queue_size = queue_num;
    if (queue_num == 0u || queue_num < min_queue_size) return -1;
    outl((uint16_t)(io_base + VIRTIO_PCI_QUEUE_PFN), virtio_ptr32(queue) >> 12);
    return inl((uint16_t)(io_base + VIRTIO_PCI_QUEUE_PFN)) == 0u ? -1 : 0;
}

void virtio_pci_notify_queue_legacy(uint32_t io_base, uint16_t queue_index) {
    outw((uint16_t)(io_base + VIRTIO_PCI_QUEUE_NOTIFY), queue_index);
}

void virtio_pci_fail_legacy(uint32_t io_base) {
    if (io_base) virtio_pci_set_status_legacy(io_base, VIRTIO_STATUS_FAILED);
}

int virtio_pci_probe_device(uint16_t legacy_device_id, uint16_t modern_device_id,
                            virtio_pci_device_t *out, uint32_t max_devices,
                            int (*legacy_extra_match)(uint8_t class_code, uint8_t subclass)) {
    uint32_t count = 0;
    uint16_t vendor;
    uint16_t device;
    uint16_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t class_code;
    uint8_t subclass;

    if (!out || max_devices == 0u) return 0;
    memset(out, 0, sizeof(out[0]) * max_devices);

    for (bus = 0; bus < 256u && count < max_devices; bus++) {
        for (dev = 0; dev < 32u && count < max_devices; dev++) {
            for (func = 0; func < 8u && count < max_devices; func++) {
                vendor = pci_read16((uint8_t)bus, dev, func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) continue;
                device = pci_read16((uint8_t)bus, dev, func, PCI_OFFSET_DEVICE);
                class_code = pci_read8((uint8_t)bus, dev, func, PCI_OFFSET_CLASS);
                subclass = pci_read8((uint8_t)bus, dev, func, PCI_OFFSET_SUBCLASS);

                if (vendor == VIRTIO_PCI_VENDOR_ID && device == legacy_device_id &&
                    (!legacy_extra_match || legacy_extra_match(class_code, subclass))) {
                    out[count].bus = (uint8_t)bus;
                    out[count].dev = dev;
                    out[count].func = func;
                    out[count].vendor_id = vendor;
                    out[count].device_id = device;
                    out[count].class_code = class_code;
                    out[count].subclass = subclass;
                    out[count].kind = VIRTIO_TRANSPORT_LEGACY_PCI;
                    out[count].io_base = virtio_pci_bar_base((uint8_t)bus, dev, func, 0u, 1u);
                    out[count].mmio_base = virtio_pci_bar_base((uint8_t)bus, dev, func, 1u, 0u);
                    count++;
                } else if (virtio_pci_is_modern_device(vendor, device, modern_device_id)) {
                    out[count].bus = (uint8_t)bus;
                    out[count].dev = dev;
                    out[count].func = func;
                    out[count].vendor_id = vendor;
                    out[count].device_id = device;
                    out[count].class_code = class_code;
                    out[count].subclass = subclass;
                    out[count].kind = VIRTIO_TRANSPORT_MODERN_PCI;
                    out[count].io_base = virtio_pci_bar_base((uint8_t)bus, dev, func, 0u, 1u);
                    out[count].mmio_base = virtio_pci_bar_base((uint8_t)bus, dev, func, 0u, 0u);
                    count++;
                }
            }
        }
    }
    return (int)count;
}
