#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "types.h"

void virtio_blk_init(void);
uint32_t virtio_blk_device_count(void);

#endif /* VIRTIO_BLK_H */
