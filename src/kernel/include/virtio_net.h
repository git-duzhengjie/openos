#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "types.h"

/* ---- virtio-net 特性位（legacy） ---- */
#define VIRTIO_NET_F_CSUM        (1u << 0)
#define VIRTIO_NET_F_GUEST_CSUM  (1u << 1)
#define VIRTIO_NET_F_MAC         (1u << 5)
#define VIRTIO_NET_F_GSO         (1u << 6)
#define VIRTIO_NET_F_STATUS      (1u << 16)
#define VIRTIO_NET_F_MRG_RXBUF   (1u << 15)

/* virtio-net 队列编号（legacy）：0=接收 1=发送 */
#define VIRTIO_NET_QUEUE_RX      0u
#define VIRTIO_NET_QUEUE_TX      1u

/* virtio-net 报文头（legacy，无 MRG_RXBUF 时 10 字节） */
typedef struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

/* ---- 对外 API ---- */

/* 探测并初始化 virtio-net 网卡（开机调用） */
void virtio_net_init(void);

/* 已初始化成功的 virtio-net 设备数量 */
uint32_t virtio_net_device_count(void);

/* 获取首块网卡的 MAC 地址，写入 6 字节 out；成功返回 0 */
int virtio_net_get_mac(uint8_t out_mac[6]);

/* 发送一个以太网帧（含目的/源MAC/类型/负载，不含 CRC）；成功返回 0 */
int virtio_net_send(const void *frame, uint32_t len);

/* 轮询接收一个以太网帧到 buf（最大 buf_len）；
 * 返回收到的字节数，无包返回 0，出错返回 -1 */
int virtio_net_poll_recv(void *buf, uint32_t buf_len);

/* 串口打印 virtio-net 状态（调试用） */
void virtio_net_dump(void);

#endif /* VIRTIO_NET_H */
