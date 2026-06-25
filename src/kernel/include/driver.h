#ifndef OPENOS_KERNEL_DRIVER_H
#define OPENOS_KERNEL_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenOSDriverOps {
    int (*probe)(OpenOSDevice *device);
    void (*remove)(OpenOSDevice *device);
    int (*suspend)(OpenOSDevice *device);
    int (*resume)(OpenOSDevice *device);
} OpenOSDriverOps;

typedef struct OpenOSDriver {
    const char *name;
    OpenOSBusType bus_type;
    uint32_t vendor_id;
    uint32_t device_id;
    int match_any;
    OpenOSDriverOps ops;
    void *driver_data;
} OpenOSDriver;

void openos_driver_init(OpenOSDriver *driver,
                        const char *name,
                        OpenOSBusType bus_type,
                        uint32_t vendor_id,
                        uint32_t device_id,
                        const OpenOSDriverOps *ops);
int openos_driver_matches_device(const OpenOSDriver *driver, const OpenOSDevice *device);
int openos_driver_bind_device(const OpenOSDriver *driver, OpenOSDevice *device);
void openos_driver_unbind_device(const OpenOSDriver *driver, OpenOSDevice *device);

int openos_driver_register(OpenOSDriver *driver);
int openos_driver_unregister(OpenOSDriver *driver);
size_t openos_driver_count(void);
OpenOSDriver *openos_driver_get_at(size_t index);
void openos_driver_bind_registered_devices(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_KERNEL_DRIVER_H */
