#include "device.h"
#include "driver.h"

#define OPENOS_MAX_DEVICES 64

static void openos_device_memzero(void *ptr, size_t size) {
    unsigned char *p = (unsigned char *)ptr;
    while (size--) *p++ = 0;
}

static int openos_device_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static OpenOSDevice *g_devices[OPENOS_MAX_DEVICES];
static size_t g_device_count;

void openos_device_init(OpenOSDevice *d, const char *n, OpenOSBusType b, uint32_t v, uint32_t id) {
    if (!d) return;
    openos_device_memzero(d, sizeof(*d));
    d->name = n;
    d->bus_type = b;
    d->vendor_id = v;
    d->device_id = id;
    d->irq = -1;
}

int openos_device_add_mmio(OpenOSDevice *d, uint64_t base, uint64_t size, uint32_t flags) {
    if (!d || d->mmio_count >= OPENOS_DEVICE_MAX_MMIO_RESOURCES) return -1;
    OpenOSMmioResource *r = &d->mmio[d->mmio_count++];
    r->base = base;
    r->size = size;
    r->flags = flags;
    return 0;
}

void openos_device_set_irq(OpenOSDevice *d, int irq) { if (d) d->irq = irq; }
void openos_device_set_platform_data(OpenOSDevice *d, void *p) { if (d) d->platform_data = p; }
void openos_device_set_driver_data(OpenOSDevice *d, void *p) { if (d) d->driver_data = p; }
void *openos_device_get_driver_data(const OpenOSDevice *d) { return d ? d->driver_data : 0; }

int openos_device_register(OpenOSDevice *d) {
    if (!d || g_device_count >= OPENOS_MAX_DEVICES) return -1;
    for (size_t i = 0; i < g_device_count; ++i) if (g_devices[i] == d) return 0;
    g_devices[g_device_count++] = d;
    openos_driver_bind_registered_devices();
    return 0;
}

int openos_device_unregister(OpenOSDevice *d) {
    if (!d) return -1;
    for (size_t i = 0; i < g_device_count; ++i) {
        if (g_devices[i] == d) {
            if (d->bound_driver) openos_driver_unbind_device(d->bound_driver, d);
            for (size_t j = i; j + 1 < g_device_count; ++j) g_devices[j] = g_devices[j + 1];
            g_devices[--g_device_count] = 0;
            return 0;
        }
    }
    return -1;
}

OpenOSDevice *openos_device_find(const char *n) {
    if (!n) return 0;
    for (size_t i = 0; i < g_device_count; ++i) {
        OpenOSDevice *d = g_devices[i];
        if (d && openos_device_streq(d->name, n)) return d;
    }
    return 0;
}

size_t openos_device_count(void) { return g_device_count; }
OpenOSDevice *openos_device_get_at(size_t i) { return i < g_device_count ? g_devices[i] : 0; }
