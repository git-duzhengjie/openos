/* ============================================================
 * openos - virtio-gpu early PCI probe skeleton
 * Early display validation path for QEMU virt / cross-arch boots.
 * ============================================================ */
#include "../include/virtio_gpu.h"
#include "../include/virtio.h"
#include "../include/serial.h"

#define VIRTIO_GPU_MAX_DEVICES 4u

static virtio_pci_device_t virtio_gpu_devices[VIRTIO_GPU_MAX_DEVICES];
static uint32_t virtio_gpu_count;

void virtio_gpu_init(void) {
    int found;
    virtio_gpu_count = 0u;
    found = virtio_pci_probe_device(VIRTIO_LEGACY_GPU_DEVICE,
                                    VIRTIO_MODERN_GPU_ID,
                                    virtio_gpu_devices,
                                    VIRTIO_GPU_MAX_DEVICES,
                                    0);
    if (found <= 0) return;
    virtio_gpu_count = (uint32_t)found;
    serial_write("[virtio-gpu] detected ");
    serial_write_hex(virtio_gpu_count);
    serial_write(" device(s); framebuffer backend hook pending\n");
}

uint32_t virtio_gpu_device_count(void) {
    return virtio_gpu_count;
}
