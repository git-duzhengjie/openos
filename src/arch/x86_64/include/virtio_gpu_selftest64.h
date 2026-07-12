#ifndef OPENOS_ARCH_X86_64_VIRTIO_GPU_SELFTEST64_H
#define OPENOS_ARCH_X86_64_VIRTIO_GPU_SELFTEST64_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.4 virtio-gpu 2D driver selftest. When a virtio-gpu-pci device is
 * present the bring-up (modern transport + 2D pipeline) has already run at
 * boot; this test verifies the driver's public surface: device_count / valid
 * geometry / non-NULL backing store, draws a probe pattern into the backing
 * store and flushes it to the host scanout (TRANSFER_TO_HOST_2D +
 * RESOURCE_FLUSH), and checks clipping / out-of-range flush handling.
 *
 * When NO device is present (default boot, GOP-only) the test PASSES as a
 * no-op ("device absent") — the driver must degrade gracefully.
 *
 * Returns 1 always (advisory diagnostic; never blocks boot).
 */
int arch_x86_64_virtio_gpu_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_VIRTIO_GPU_SELFTEST64_H */
