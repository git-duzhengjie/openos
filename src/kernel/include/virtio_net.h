#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "types.h"

void virtio_net_init(void);
uint32_t virtio_net_device_count(void);

#endif /* VIRTIO_NET_H */
