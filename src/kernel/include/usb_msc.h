/* ============================================================
 * openos - USB Mass Storage Class (BOT + SCSI) 驱动接口
 * ------------------------------------------------------------
 * 协议栈：USB MSC → Bulk-Only Transport(BOT) → SCSI 命令集
 * 依赖：xhci64.c 提供的 bulk 传输原语 + blockdev 抽象层
 * ============================================================ */

#ifndef USB_MSC_H
#define USB_MSC_H

#include "types.h"

/* ---- USB Mass Storage 接口分类常量 ---- */
#define USB_CLASS_MASS_STORAGE   0x08u   /* bInterfaceClass */
#define USB_MSC_SUBCLASS_SCSI    0x06u   /* SCSI transparent command set */
#define USB_MSC_PROTO_BOT        0x50u   /* Bulk-Only Transport */

/* ---- BOT class-specific 请求（EP0 控制传输） ---- */
#define USB_MSC_REQ_RESET        0xFFu   /* Bulk-Only Mass Storage Reset */
#define USB_MSC_REQ_GET_MAX_LUN  0xFEu   /* Get Max LUN */

/* ============================================================
 * Command Block Wrapper (CBW) — 主机→设备，固定 31 字节
 * ============================================================ */
#define CBW_SIGNATURE   0x43425355u  /* "USBC" (小端) */
#define CSW_SIGNATURE   0x53425355u  /* "USBS" (小端) */

#define CBW_FLAG_DATA_IN   0x80u   /* bmCBWFlags: 数据方向 IN(设备→主机) */
#define CBW_FLAG_DATA_OUT  0x00u   /* 数据方向 OUT(主机→设备) */

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;          /* 0x43425355 */
    uint32_t dCBWTag;                /* 命令标签，CSW 原样返回 */
    uint32_t dCBWDataTransferLength; /* 期望的数据阶段字节数 */
    uint8_t  bmCBWFlags;             /* bit7: 方向 */
    uint8_t  bCBWLUN;                /* 低 4 位有效 */
    uint8_t  bCBWCBLength;           /* SCSI 命令块长度(1..16) */
    uint8_t  CBWCB[16];              /* SCSI 命令块 */
} usb_msc_cbw_t;

/* ============================================================
 * Command Status Wrapper (CSW) — 设备→主机，固定 13 字节
 * ============================================================ */
#define CSW_STATUS_PASSED   0x00u
#define CSW_STATUS_FAILED   0x01u
#define CSW_STATUS_PHASE_ERR 0x02u

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;    /* 0x53425355 */
    uint32_t dCSWTag;          /* 与 CBW 的 dCBWTag 对应 */
    uint32_t dCSWDataResidue;  /* 未传输的字节数 */
    uint8_t  bCSWStatus;       /* 0=通过 1=失败 2=相位错误 */
} usb_msc_csw_t;

/* ============================================================
 * SCSI 命令操作码（Transparent Command Set 子集）
 * ============================================================ */
#define SCSI_TEST_UNIT_READY  0x00u
#define SCSI_REQUEST_SENSE    0x03u
#define SCSI_INQUIRY          0x12u
#define SCSI_READ_CAPACITY10  0x25u
#define SCSI_READ_10          0x28u
#define SCSI_WRITE_10         0x2Au

/* INQUIRY 标准数据（前 36 字节子集） */
typedef struct __attribute__((packed)) {
    uint8_t  peripheral;       /* bits[4:0]=device type, 0=直接访问块设备 */
    uint8_t  rmb;              /* bit7=可移动介质 */
    uint8_t  version;
    uint8_t  response_format;
    uint8_t  additional_len;
    uint8_t  flags[3];
    char     vendor_id[8];
    char     product_id[16];
    char     product_rev[4];
} scsi_inquiry_data_t;

/* READ CAPACITY(10) 返回数据（8 字节，大端） */
typedef struct __attribute__((packed)) {
    uint32_t last_lba_be;      /* 最后一个可寻址 LBA（大端） */
    uint32_t block_len_be;     /* 每块字节数（大端） */
} scsi_read_capacity10_data_t;

/* ============================================================
 * MSC 逻辑设备（一个 BOT 接口 = 一个 U 盘）
 * ============================================================ */
#define USB_MSC_MAX_DEVS  4

typedef struct usb_msc_dev {
    int      used;
    uint32_t slot_id;          /* xHCI slot */
    uint8_t  iface_num;        /* bInterfaceNumber */
    uint8_t  max_lun;          /* Get Max LUN 结果 */
    uint8_t  ep_in;            /* Bulk IN 端点地址(含方向) */
    uint8_t  ep_out;           /* Bulk OUT 端点地址 */
    uint32_t tag;              /* CBW 递增标签 */
    /* 容量信息（READ CAPACITY 填充） */
    uint32_t block_count;      /* 总块数 = last_lba + 1 */
    uint32_t block_size;       /* 每块字节数（一般 512） */
    char     vendor[9];
    char     product[17];
} usb_msc_dev_t;

/* ============================================================
 * 对外接口
 * ============================================================ */

/* 由 xHCI 枚举回调：发现一个 BOT 接口后初始化 MSC 逻辑设备。
 * 完成 GET MAX LUN / INQUIRY / TEST UNIT READY / READ CAPACITY，
 * 成功后注册进 blockdev（名称 usbN）。
 * 返回 0 成功，<0 失败。 */
int usb_msc_attach(uint32_t slot_id, uint8_t iface_num,
                   uint8_t ep_in, uint8_t ep_out);

/* 读/写块（LBA 寻址，512 字节/块）。供 blockdev ops 回调。 */
int usb_msc_read_blocks(usb_msc_dev_t *dev, uint32_t lba, uint32_t count, void *buf);
int usb_msc_write_blocks(usb_msc_dev_t *dev, uint32_t lba, uint32_t count, const void *buf);

/* 探测：是否至少有一个已就绪的 U 盘 */
int usb_msc_present(void);

/* 获取第一个已就绪 U 盘（供 blockdev_hw 注册用） */
usb_msc_dev_t *usb_msc_get(uint32_t index);
uint32_t usb_msc_count(void);

#endif /* USB_MSC_H */
