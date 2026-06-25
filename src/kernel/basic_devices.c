#include "basic_devices.h"
#include "device.h"
#include "driver.h"

#define OPENOS_VENDOR_GENERIC 0x4f534f53u
#define OPENOS_DEV_UART      0x00000001u
#define OPENOS_DEV_DISPLAY   0x00000002u
#define OPENOS_DEV_TIMER     0x00000003u
#define OPENOS_DEV_IRQCTRL   0x00000004u
#define OPENOS_DEV_BLOCK     0x00000005u
#define OPENOS_DEV_INPUT     0x00000006u

static OpenOSDevice g_uart0;
static OpenOSDevice g_display0;
static OpenOSDevice g_timer0;
static OpenOSDevice g_irqctrl0;
static OpenOSDevice g_block0;
static OpenOSDevice g_input0;
static OpenOSDriver g_platform_driver;
static int g_registered;

static int openos_basic_probe(OpenOSDevice *device) {
    (void)device;
    return 0;
}

void openos_basic_devices_register(void) {
    if (g_registered) return;
    g_registered = 1;

    OpenOSDriverOps ops;
    ops.probe = openos_basic_probe;
    ops.remove = 0;
    ops.suspend = 0;
    ops.resume = 0;
    openos_driver_init(&g_platform_driver, "generic-platform", OPENOS_BUS_PLATFORM,
                       OPENOS_DEVICE_ANY_ID, OPENOS_DEVICE_ANY_ID, &ops);
    openos_driver_register(&g_platform_driver);

    openos_device_init(&g_uart0, "uart0", OPENOS_BUS_PLATFORM, OPENOS_VENDOR_GENERIC, OPENOS_DEV_UART);
    openos_device_add_mmio(&g_uart0, 0x3f8u, 8u, 0u);
    openos_device_set_irq(&g_uart0, 4);
    openos_device_register(&g_uart0);

    openos_device_init(&g_display0, "display0", OPENOS_BUS_PLATFORM, OPENOS_VENDOR_GENERIC, OPENOS_DEV_DISPLAY);
    openos_device_register(&g_display0);

    openos_device_init(&g_timer0, "timer0", OPENOS_BUS_PLATFORM, OPENOS_VENDOR_GENERIC, OPENOS_DEV_TIMER);
    openos_device_set_irq(&g_timer0, 0);
    openos_device_register(&g_timer0);

    openos_device_init(&g_irqctrl0, "irqctrl0", OPENOS_BUS_PLATFORM, OPENOS_VENDOR_GENERIC, OPENOS_DEV_IRQCTRL);
    openos_device_register(&g_irqctrl0);

    openos_device_init(&g_block0, "block0", OPENOS_BUS_PLATFORM, OPENOS_VENDOR_GENERIC, OPENOS_DEV_BLOCK);
    openos_device_register(&g_block0);

    openos_device_init(&g_input0, "input0", OPENOS_BUS_PLATFORM, OPENOS_VENDOR_GENERIC, OPENOS_DEV_INPUT);
    openos_device_set_irq(&g_input0, 1);
    openos_device_register(&g_input0);
}
