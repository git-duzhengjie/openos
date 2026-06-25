/* ============================================================
 * openos - virtio-input early PCI probe skeleton
 * Mobile early input validation backend for QEMU virt.
 * ============================================================ */
#include "../include/virtio_input.h"
#include "../include/virtio.h"
#include "../include/serial.h"

#define VIRTIO_INPUT_MAX_DEVICES 4u

static virtio_pci_device_t virtio_input_devices[VIRTIO_INPUT_MAX_DEVICES];
static uint32_t virtio_input_count;

void virtio_input_init(void) {
    int found;
    virtio_input_count = 0u;
    found = virtio_pci_probe_device(VIRTIO_LEGACY_INPUT_DEVICE,
                                    VIRTIO_MODERN_INPUT_ID,
                                    virtio_input_devices,
                                    VIRTIO_INPUT_MAX_DEVICES,
                                    0);
    if (found <= 0) return;
    virtio_input_count = (uint32_t)found;
    serial_write("[virtio-input] detected ");
    serial_write_hex(virtio_input_count);
    serial_write(" device(s)\n");
}

uint32_t virtio_input_device_count(void) {
    return virtio_input_count;
}
