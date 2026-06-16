/* ============================================================
 * openos - AHCI/SATA discovery block driver scaffold
 * ============================================================ */
#include "../include/ahci.h"
#include "../include/blockdev.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../fs/vfs.h"

#define AHCI_SECTOR_SIZE 512u
#define AHCI_MAX_CONTROLLERS 4u
#define AHCI_MAX_PORTS 32u
#define AHCI_MAX_DEVICES 8u

#define AHCI_PCI_CLASS_MASS_STORAGE 0x01u
#define AHCI_PCI_SUBCLASS_SATA      0x06u
#define AHCI_PCI_PROGIF_AHCI        0x01u

#define AHCI_PORT_DET_PRESENT 0x3u
#define AHCI_PORT_IPM_ACTIVE  0x1u

#define AHCI_SIG_SATA  0x00000101u
#define AHCI_SIG_ATAPI 0xEB140101u

typedef volatile struct ahci_port_regs {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} ahci_port_regs_t;

typedef volatile struct ahci_hba_mem {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t reserved[0xA0u - 0x2Cu];
    uint8_t vendor[0x100u - 0xA0u];
    ahci_port_regs_t ports[AHCI_MAX_PORTS];
} ahci_hba_mem_t;

typedef struct ahci_device {
    uint8_t present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t port_index;
    uint32_t abar;
    uint32_t signature;
    uint32_t sectors;
    char name[16];
    blockdev_t *blockdev;
    ahci_port_regs_t *regs;
} ahci_device_t;

static ahci_device_t ahci_devices[AHCI_MAX_DEVICES];
static blockdev_ops_t ahci_ops;
static uint32_t ahci_count;

static int ahci_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    return -1;
}

static int ahci_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    (void)dev;
    (void)lba;
    (void)count;
    (void)buf;
    return -1;
}

uint32_t ahci_device_count(void) {
    return ahci_count;
}

static int ahci_port_is_active(ahci_port_regs_t *port) {
    uint32_t ssts = port->ssts;
    uint32_t det = ssts & 0x0Fu;
    uint32_t ipm = (ssts >> 8) & 0x0Fu;
    return det == AHCI_PORT_DET_PRESENT && ipm == AHCI_PORT_IPM_ACTIVE;
}

static void ahci_make_name(char *out, uint32_t index) {
    out[0] = 'a';
    out[1] = 'h';
    out[2] = 'c';
    out[3] = 'i';
    out[4] = (char)('0' + (char)(index % 10u));
    out[5] = '\0';
}

static void ahci_make_dev_path(char *out, const char *name) {
    out[0] = '/';
    out[1] = 'd';
    out[2] = 'e';
    out[3] = 'v';
    out[4] = '/';
    strcpy(out + 5, name);
}

static const char *ahci_signature_name(uint32_t sig) {
    if (sig == AHCI_SIG_SATA) return "SATA";
    if (sig == AHCI_SIG_ATAPI) return "SATAPI";
    return "UNKNOWN";
}

static void ahci_register_port(uint8_t bus, uint8_t dev, uint8_t func,
                               uint32_t abar, ahci_hba_mem_t *hba, uint32_t port_index) {
    ahci_device_t *adev;
    ahci_port_regs_t *port;
    char path[32];
    uint32_t sig;

    if (ahci_count >= AHCI_MAX_DEVICES) return;

    port = (ahci_port_regs_t *)&hba->ports[port_index];
    if (!ahci_port_is_active(port)) return;

    sig = port->sig;
    if (sig != AHCI_SIG_SATA && sig != AHCI_SIG_ATAPI) return;

    adev = &ahci_devices[ahci_count];
    memset(adev, 0, sizeof(*adev));
    adev->present = 1;
    adev->bus = bus;
    adev->dev = dev;
    adev->func = func;
    adev->port_index = (uint8_t)port_index;
    adev->abar = abar;
    adev->signature = sig;
    adev->sectors = 1u;
    adev->regs = port;
    ahci_make_name(adev->name, ahci_count);

    if (blockdev_register(adev->name, 4u, ahci_count,
                          AHCI_SECTOR_SIZE, adev->sectors,
                          &ahci_ops, adev) < 0) {
        memset(adev, 0, sizeof(*adev));
        return;
    }

    adev->blockdev = blockdev_find(adev->name);
    if (!adev->blockdev) {
        (void)blockdev_unregister(adev->name);
        memset(adev, 0, sizeof(*adev));
        return;
    }

    ahci_make_dev_path(path, adev->name);
    (void)vfs_mknod(path, FS_BLOCK_DEVICE | 0660, adev->name);

    serial_write("[AHCI] registered ");
    serial_write(adev->name);
    serial_write(" port=");
    serial_write_hex(port_index);
    serial_write(" type=");
    serial_write(ahci_signature_name(sig));
    serial_write("\n");

    ahci_count++;
}

static void ahci_probe_controller(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t abar;
    ahci_hba_mem_t *hba;
    uint32_t implemented;
    uint32_t port;

    abar = pci_read32(bus, dev, func, PCI_OFFSET_BAR5) & 0xFFFFFFF0u;
    if (abar == 0u) return;

    hba = (ahci_hba_mem_t *)(uintptr_t)abar;
    implemented = hba->pi;

    serial_write("[AHCI] controller found ABAR=0x");
    serial_write_hex(abar);
    serial_write(" ports=0x");
    serial_write_hex(implemented);
    serial_write("\n");

    for (port = 0; port < AHCI_MAX_PORTS; port++) {
        if ((implemented & (1u << port)) != 0u) {
            ahci_register_port(bus, dev, func, abar, hba, port);
        }
    }
}

void ahci_init(void) {
    uint16_t bus;
    uint16_t dev;
    uint16_t func;
    uint32_t controllers = 0;

    memset(ahci_devices, 0, sizeof(ahci_devices));
    memset(&ahci_ops, 0, sizeof(ahci_ops));
    ahci_ops.read_blocks = ahci_read_blocks;
    ahci_ops.write_blocks = ahci_write_blocks;
    ahci_count = 0;

    serial_write("[AHCI] probing SATA AHCI controllers\n");
    for (bus = 0; bus < 256u; bus++) {
        for (dev = 0; dev < 32u; dev++) {
            for (func = 0; func < 8u; func++) {
                uint16_t vendor = pci_read16((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    if (func == 0u) break;
                    continue;
                }

                if (pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 2u) == AHCI_PCI_CLASS_MASS_STORAGE &&
                    pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS + 1u) == AHCI_PCI_SUBCLASS_SATA &&
                    pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS) == AHCI_PCI_PROGIF_AHCI) {
                    ahci_probe_controller((uint8_t)bus, (uint8_t)dev, (uint8_t)func);
                    controllers++;
                    if (controllers >= AHCI_MAX_CONTROLLERS) return;
                }

                if ((pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_HEADER) & 0x80u) == 0u) break;
            }
        }
    }

    if (controllers == 0u) serial_write("[AHCI] no controller\n");
    else serial_write("[AHCI] probe complete\n");
}
