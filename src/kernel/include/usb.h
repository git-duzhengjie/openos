#ifndef USB_H
#define USB_H

#include "types.h"

#define USB_MAX_HOST_CONTROLLERS 8
#define USB_MAX_DEVICES          16
#define USB_MAX_ENDPOINTS        8

#define USB_CLASS_HID            0x03
#define USB_CLASS_MASS_STORAGE   0x08
#define USB_CLASS_HUB            0x09
#define USB_CLASS_VENDOR         0xFF

#define USB_DESC_DEVICE          0x01
#define USB_DESC_CONFIG          0x02
#define USB_DESC_STRING          0x03
#define USB_DESC_INTERFACE       0x04
#define USB_DESC_ENDPOINT        0x05
#define USB_DESC_HID             0x21
#define USB_DESC_HID_REPORT      0x22

typedef enum usb_hc_type {
    USB_HC_UNKNOWN = 0,
    USB_HC_UHCI,
    USB_HC_OHCI,
    USB_HC_EHCI,
    USB_HC_XHCI
} usb_hc_type_t;

typedef enum usb_speed {
    USB_SPEED_UNKNOWN = 0,
    USB_SPEED_LOW,
    USB_SPEED_FULL,
    USB_SPEED_HIGH,
    USB_SPEED_SUPER
} usb_speed_t;

typedef enum usb_device_state {
    USB_DEV_EMPTY = 0,
    USB_DEV_ATTACHED,
    USB_DEV_POWERED,
    USB_DEV_DEFAULT,
    USB_DEV_ADDRESS,
    USB_DEV_CONFIGURED
} usb_device_state_t;

typedef struct usb_device_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct usb_config_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct usb_interface_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct usb_endpoint_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct usb_setup_packet {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

typedef struct usb_host_controller {
    uint8_t used;
    usb_hc_type_t type;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t irq;
    uint16_t io_base;
    uint32_t mem_base;
    uint32_t pci_class;
    uint32_t devices_seen;
} usb_host_controller_t;

typedef struct usb_device {
    uint8_t used;
    uint8_t address;
    uint8_t host_index;
    uint8_t port;
    usb_speed_t speed;
    usb_device_state_t state;
    uint8_t max_packet0;
    uint8_t configuration;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint16_t vendor_id;
    uint16_t product_id;
} usb_device_t;

typedef struct usb_stats {
    uint32_t scans;
    uint32_t host_count;
    uint32_t device_count;
    uint32_t uhci_count;
    uint32_t ohci_count;
    uint32_t ehci_count;
    uint32_t xhci_count;
} usb_stats_t;

void usb_init(void);
void usb_rescan(void);
uint32_t usb_host_count(void);
uint32_t usb_device_count(void);
const usb_host_controller_t *usb_get_host(uint32_t index);
const usb_device_t *usb_get_device(uint32_t index);
const usb_stats_t *usb_get_stats(void);
const char *usb_hc_type_name(usb_hc_type_t type);
const char *usb_speed_name(usb_speed_t speed);
const char *usb_device_state_name(usb_device_state_t state);
void usb_print_info(void);

#endif /* USB_H */
