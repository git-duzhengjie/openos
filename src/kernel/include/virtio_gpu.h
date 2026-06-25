#ifndef OPENOS_KERNEL_VIRTIO_GPU_H
#define OPENOS_KERNEL_VIRTIO_GPU_H

#include "types.h"

void virtio_gpu_init(void);
uint32_t virtio_gpu_device_count(void);

#endif /* OPENOS_KERNEL_VIRTIO_GPU_H */
