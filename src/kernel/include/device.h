#ifndef OPENOS_KERNEL_DEVICE_H
#define OPENOS_KERNEL_DEVICE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENOS_DEVICE_MAX_MMIO_RESOURCES 4
#define OPENOS_DEVICE_ANY_ID 0xffffffffu

typedef enum OpenOSBusType {
    OPENOS_BUS_PLATFORM = 0,
    OPENOS_BUS_PCI,
    OPENOS_BUS_USB,
    OPENOS_BUS_VIRTIO,
    OPENOS_BUS_I2C,
    OPENOS_BUS_SPI,
    OPENOS_BUS_GPIO,
    OPENOS_BUS_COUNT
} OpenOSBusType;

typedef struct OpenOSMmioResource {
    uint64_t base;
    uint64_t size;
    uint32_t flags;
} OpenOSMmioResource;

struct OpenOSDriver;

typedef struct OpenOSDevice {
    const char *name;
    OpenOSBusType bus_type;
    uint32_t vendor_id;
    uint32_t device_id;
    OpenOSMmioResource mmio[OPENOS_DEVICE_MAX_MMIO_RESOURCES];
    uint32_t mmio_count;
    int irq;
    void *platform_data;
    void *driver_data;
    const struct OpenOSDriver *bound_driver;
} OpenOSDevice;

void openos_device_init(OpenOSDevice *device,
                        const char *name,
                        OpenOSBusType bus_type,
                        uint32_t vendor_id,
                        uint32_t device_id);
int openos_device_add_mmio(OpenOSDevice *device, uint64_t base, uint64_t size, uint32_t flags);
void openos_device_set_irq(OpenOSDevice *device, int irq);
void openos_device_set_platform_data(OpenOSDevice *device, void *platform_data);
void openos_device_set_driver_data(OpenOSDevice *device, void *driver_data);
void *openos_device_get_driver_data(const OpenOSDevice *device);

int openos_device_register(OpenOSDevice *device);
int openos_device_unregister(OpenOSDevice *device);
OpenOSDevice *openos_device_find(const char *name);
size_t openos_device_count(void);
OpenOSDevice *openos_device_get_at(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_DEVICE_H */
