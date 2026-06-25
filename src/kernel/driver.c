#include "driver.h"

#define OPENOS_MAX_DRIVERS 64

static void openos_driver_memzero(void *ptr, size_t size) {
    unsigned char *p = (unsigned char *)ptr;
    while (size--) *p++ = 0;
}

static OpenOSDriver *g_drivers[OPENOS_MAX_DRIVERS];
static size_t g_driver_count;

void openos_driver_init(OpenOSDriver *d, const char *n, OpenOSBusType b, uint32_t v, uint32_t id, const OpenOSDriverOps *ops) {
    if (!d) return;
    openos_driver_memzero(d, sizeof(*d));
    d->name = n;
    d->bus_type = b;
    d->vendor_id = v;
    d->device_id = id;
    d->match_any = (v == OPENOS_DEVICE_ANY_ID && id == OPENOS_DEVICE_ANY_ID);
    if (ops) d->ops = *ops;
}

int openos_driver_matches_device(const OpenOSDriver *drv, const OpenOSDevice *dev) {
    if (!drv || !dev) return 0;
    if (drv->bus_type != dev->bus_type) return 0;
    if (drv->match_any) return 1;
    if (drv->vendor_id != OPENOS_DEVICE_ANY_ID && drv->vendor_id != dev->vendor_id) return 0;
    if (drv->device_id != OPENOS_DEVICE_ANY_ID && drv->device_id != dev->device_id) return 0;
    return 1;
}

int openos_driver_bind_device(const OpenOSDriver *drv, OpenOSDevice *dev) {
    if (!drv || !dev || dev->bound_driver) return -1;
    if (!openos_driver_matches_device(drv, dev)) return -1;
    if (drv->ops.probe) {
        int rc = drv->ops.probe(dev);
        if (rc != 0) return rc;
    }
    dev->bound_driver = drv;
    return 0;
}

void openos_driver_unbind_device(const OpenOSDriver *drv, OpenOSDevice *dev) {
    if (!drv || !dev || dev->bound_driver != drv) return;
    if (drv->ops.remove) drv->ops.remove(dev);
    dev->bound_driver = 0;
}

int openos_driver_register(OpenOSDriver *drv) {
    if (!drv || g_driver_count >= OPENOS_MAX_DRIVERS) return -1;
    for (size_t i = 0; i < g_driver_count; ++i) if (g_drivers[i] == drv) return 0;
    g_drivers[g_driver_count++] = drv;
    openos_driver_bind_registered_devices();
    return 0;
}

int openos_driver_unregister(OpenOSDriver *drv) {
    if (!drv) return -1;
    for (size_t i = 0; i < openos_device_count(); ++i) {
        OpenOSDevice *dev = openos_device_get_at(i);
        if (dev && dev->bound_driver == drv) openos_driver_unbind_device(drv, dev);
    }
    for (size_t i = 0; i < g_driver_count; ++i) {
        if (g_drivers[i] == drv) {
            for (size_t j = i; j + 1 < g_driver_count; ++j) g_drivers[j] = g_drivers[j + 1];
            g_drivers[--g_driver_count] = 0;
            return 0;
        }
    }
    return -1;
}

size_t openos_driver_count(void) { return g_driver_count; }
OpenOSDriver *openos_driver_get_at(size_t i) { return i < g_driver_count ? g_drivers[i] : 0; }

void openos_driver_bind_registered_devices(void) {
    for (size_t di = 0; di < openos_device_count(); ++di) {
        OpenOSDevice *dev = openos_device_get_at(di);
        if (!dev || dev->bound_driver) continue;
        for (size_t ri = 0; ri < g_driver_count; ++ri) {
            if (openos_driver_bind_device(g_drivers[ri], dev) == 0) break;
        }
    }
}
