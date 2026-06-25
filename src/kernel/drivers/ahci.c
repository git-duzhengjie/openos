/* ============================================================
 * openos - AHCI/SATA block driver
 * ============================================================ */
#include "../include/ahci.h"
#include "../include/blockdev.h"
#include "../include/pci.h"
#include "../include/serial.h"
#include "../include/string.h"
#include "../core/fs/vfs.h"

#define AHCI_SECTOR_SIZE 512u
#define AHCI_MAX_CONTROLLERS 4u
#define AHCI_MAX_PORTS 32u
#define AHCI_MAX_DEVICES 8u
#define AHCI_CMD_SLOTS 32u
#define AHCI_CMD_TABLE_PRDT_COUNT 1u
#define AHCI_MAX_SECTORS_PER_CMD 128u
#define AHCI_WAIT_LIMIT 1000000u

#define AHCI_PCI_CLASS_MASS_STORAGE 0x01u
#define AHCI_PCI_SUBCLASS_SATA      0x06u
#define AHCI_PCI_PROGIF_AHCI        0x01u
#define AHCI_PCI_COMMAND            0x04u
#define AHCI_PCI_CMD_IO             0x0001u
#define AHCI_PCI_CMD_MEM            0x0002u
#define AHCI_PCI_CMD_BUSMASTER      0x0004u

#define AHCI_GHC_AE                 0x80000000u
#define AHCI_GHC_HR                 0x00000001u

#define AHCI_PORT_DET_PRESENT       0x3u
#define AHCI_PORT_IPM_ACTIVE        0x1u
#define AHCI_PORT_CMD_ST            0x0001u
#define AHCI_PORT_CMD_FRE           0x0010u
#define AHCI_PORT_CMD_FR            0x4000u
#define AHCI_PORT_CMD_CR            0x8000u
#define AHCI_PORT_TFD_BSY           0x80u
#define AHCI_PORT_TFD_DRQ           0x08u
#define AHCI_PORT_IS_TFES           0x40000000u
#define AHCI_PORT_SERR_CLEAR        0xFFFFFFFFu

#define AHCI_SIG_SATA               0x00000101u
#define AHCI_SIG_ATAPI              0xEB140101u

#define AHCI_FIS_TYPE_REG_H2D       0x27u
#define AHCI_ATA_DEV_LBA            0x40u
#define AHCI_ATA_CMD_IDENTIFY       0xECu
#define AHCI_ATA_CMD_READ_DMA_EXT   0x25u
#define AHCI_ATA_CMD_WRITE_DMA_EXT  0x35u

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

typedef struct ahci_cmd_header {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct ahci_prdt_entry {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved;
    uint32_t dbc_i;
} __attribute__((packed)) ahci_prdt_entry_t;

typedef struct ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    ahci_prdt_entry_t prdt[AHCI_CMD_TABLE_PRDT_COUNT];
} __attribute__((packed)) ahci_cmd_table_t;

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
    ahci_cmd_header_t *cmd_list;
    uint8_t *fis;
    ahci_cmd_table_t *cmd_table;
} ahci_device_t;

static ahci_device_t ahci_devices[AHCI_MAX_DEVICES];
static blockdev_ops_t ahci_ops;
static uint32_t ahci_count;

static ahci_cmd_header_t ahci_cmd_lists[AHCI_MAX_DEVICES][AHCI_CMD_SLOTS] __attribute__((aligned(1024)));
static uint8_t ahci_fis_buffers[AHCI_MAX_DEVICES][256] __attribute__((aligned(256)));
static ahci_cmd_table_t ahci_cmd_tables[AHCI_MAX_DEVICES] __attribute__((aligned(128)));
static uint8_t ahci_identify_buffers[AHCI_MAX_DEVICES][AHCI_SECTOR_SIZE] __attribute__((aligned(4)));

static uint32_t ahci_ptr32(const void *ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

static int ahci_wait_clear(volatile uint32_t *reg, uint32_t mask) {
    uint32_t i;
    for (i = 0; i < AHCI_WAIT_LIMIT; i++) {
        if ((*reg & mask) == 0u) return 0;
    }
    return -1;
}

static int ahci_wait_settle_tfd(ahci_port_regs_t *port) {
    uint32_t i;
    for (i = 0; i < AHCI_WAIT_LIMIT; i++) {
        if ((port->tfd & (AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) == 0u) return 0;
    }
    return -1;
}

static void ahci_stop_port(ahci_port_regs_t *port) {
    port->cmd &= ~AHCI_PORT_CMD_ST;
    (void)ahci_wait_clear(&port->cmd, AHCI_PORT_CMD_CR);
    port->cmd &= ~AHCI_PORT_CMD_FRE;
    (void)ahci_wait_clear(&port->cmd, AHCI_PORT_CMD_FR);
}

static void ahci_start_port(ahci_port_regs_t *port) {
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_setup_port(ahci_device_t *adev, uint32_t index) {
    ahci_port_regs_t *port = adev->regs;

    ahci_stop_port(port);
    memset(ahci_cmd_lists[index], 0, sizeof(ahci_cmd_lists[index]));
    memset(ahci_fis_buffers[index], 0, sizeof(ahci_fis_buffers[index]));
    memset(&ahci_cmd_tables[index], 0, sizeof(ahci_cmd_tables[index]));

    adev->cmd_list = ahci_cmd_lists[index];
    adev->fis = ahci_fis_buffers[index];
    adev->cmd_table = &ahci_cmd_tables[index];

    port->clb = ahci_ptr32(adev->cmd_list);
    port->clbu = 0u;
    port->fb = ahci_ptr32(adev->fis);
    port->fbu = 0u;
    port->serr = AHCI_PORT_SERR_CLEAR;
    port->is = 0xFFFFFFFFu;

    ahci_start_port(port);
    return ahci_wait_settle_tfd(port);
}

static int ahci_find_slot(ahci_port_regs_t *port) {
    uint32_t slots = port->sact | port->ci;
    uint32_t i;
    for (i = 0; i < AHCI_CMD_SLOTS; i++) {
        if ((slots & (1u << i)) == 0u) return (int)i;
    }
    return -1;
}

static void ahci_build_fis(uint8_t *fis, uint8_t command, uint32_t lba, uint16_t count) {
    memset(fis, 0, 64u);
    fis[0] = AHCI_FIS_TYPE_REG_H2D;
    fis[1] = 0x80u;
    fis[2] = command;
    fis[7] = AHCI_ATA_DEV_LBA;
    fis[4] = (uint8_t)(lba & 0xFFu);
    fis[5] = (uint8_t)((lba >> 8) & 0xFFu);
    fis[6] = (uint8_t)((lba >> 16) & 0xFFu);
    fis[8] = (uint8_t)((lba >> 24) & 0xFFu);
    fis[12] = (uint8_t)(count & 0xFFu);
    fis[13] = (uint8_t)((count >> 8) & 0xFFu);
}

static int ahci_do_command(ahci_device_t *adev, uint8_t command, uint32_t lba,
                           uint16_t sectors, void *buf, uint8_t write) {
    ahci_port_regs_t *port;
    ahci_cmd_header_t *header;
    ahci_cmd_table_t *table;
    int slot;
    uint32_t bytes;
    uint32_t i;

    if (!adev || !adev->present || !buf || sectors == 0u) return -1;
    port = adev->regs;
    if (ahci_wait_settle_tfd(port) != 0) return -1;

    slot = ahci_find_slot(port);
    if (slot < 0) return -1;

    header = &adev->cmd_list[slot];
    table = adev->cmd_table;
    bytes = (uint32_t)sectors * AHCI_SECTOR_SIZE;

    memset(header, 0, sizeof(*header));
    memset(table, 0, sizeof(*table));

    header->flags = (uint16_t)(5u | (write ? (1u << 6) : 0u));
    header->prdtl = 1u;
    header->ctba = ahci_ptr32(table);
    header->ctbau = 0u;

    ahci_build_fis(table->cfis, command, lba, sectors);
    table->prdt[0].dba = ahci_ptr32(buf);
    table->prdt[0].dbau = 0u;
    table->prdt[0].reserved = 0u;
    table->prdt[0].dbc_i = (bytes - 1u) | (1u << 31);

    port->serr = AHCI_PORT_SERR_CLEAR;
    port->is = 0xFFFFFFFFu;
    port->ci = 1u << (uint32_t)slot;

    for (i = 0; i < AHCI_WAIT_LIMIT; i++) {
        if ((port->ci & (1u << (uint32_t)slot)) == 0u) {
            if ((port->is & AHCI_PORT_IS_TFES) != 0u) return -1;
            return 0;
        }
        if ((port->is & AHCI_PORT_IS_TFES) != 0u) return -1;
    }

    return -1;
}

static int ahci_identify_device(ahci_device_t *adev, uint32_t index) {
    uint16_t *id;
    uint32_t sectors28;
    uint32_t sectors48_low;
    uint32_t sectors48_high;

    memset(ahci_identify_buffers[index], 0, sizeof(ahci_identify_buffers[index]));
    if (ahci_do_command(adev, AHCI_ATA_CMD_IDENTIFY, 0u, 1u, ahci_identify_buffers[index], 0u) != 0) {
        return -1;
    }

    id = (uint16_t *)ahci_identify_buffers[index];
    sectors48_low = ((uint32_t)id[100]) | ((uint32_t)id[101] << 16);
    sectors48_high = ((uint32_t)id[102]) | ((uint32_t)id[103] << 16);
    sectors28 = ((uint32_t)id[60]) | ((uint32_t)id[61] << 16);

    if (sectors48_low != 0u || sectors48_high != 0u) {
        adev->sectors = sectors48_high != 0u ? 0xFFFFFFFFu : sectors48_low;
    } else {
        adev->sectors = sectors28;
    }

    return adev->sectors == 0u ? -1 : 0;
}

static int ahci_transfer_blocks(ahci_device_t *adev, uint32_t lba, uint32_t count,
                                void *buf, uint8_t write) {
    uint8_t *cursor = (uint8_t *)buf;
    uint32_t remaining = count;

    if (!adev || !adev->present || !buf || count == 0u) return -1;
    if (lba >= adev->sectors || count > (adev->sectors - lba)) return -1;

    while (remaining > 0u) {
        uint16_t chunk = remaining > AHCI_MAX_SECTORS_PER_CMD ?
                         (uint16_t)AHCI_MAX_SECTORS_PER_CMD : (uint16_t)remaining;
        uint8_t command = write ? AHCI_ATA_CMD_WRITE_DMA_EXT : AHCI_ATA_CMD_READ_DMA_EXT;
        if (ahci_do_command(adev, command, lba, chunk, cursor, write) != 0) return -1;
        lba += chunk;
        cursor += (uint32_t)chunk * AHCI_SECTOR_SIZE;
        remaining -= chunk;
    }

    return 0;
}

static int ahci_read_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    ahci_device_t *adev = dev ? (ahci_device_t *)dev->private_data : 0;
    return ahci_transfer_blocks(adev, lba, count, buf, 0u);
}

static int ahci_write_blocks(blockdev_t *dev, uint32_t lba, uint32_t count, const void *buf) {
    ahci_device_t *adev = dev ? (ahci_device_t *)dev->private_data : 0;
    return ahci_transfer_blocks(adev, lba, count, (void *)buf, 1u);
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
    uint32_t index;

    if (ahci_count >= AHCI_MAX_DEVICES) return;

    port = (ahci_port_regs_t *)&hba->ports[port_index];
    if (!ahci_port_is_active(port)) return;

    sig = port->sig;
    if (sig != AHCI_SIG_SATA) return;

    index = ahci_count;
    adev = &ahci_devices[index];
    memset(adev, 0, sizeof(*adev));
    adev->present = 1;
    adev->bus = bus;
    adev->dev = dev;
    adev->func = func;
    adev->port_index = (uint8_t)port_index;
    adev->abar = abar;
    adev->signature = sig;
    adev->regs = port;
    ahci_make_name(adev->name, index);

    if (ahci_setup_port(adev, index) != 0 || ahci_identify_device(adev, index) != 0) {
        serial_write("[AHCI] port identify failed port=");
        serial_write_hex(port_index);
        serial_write(" type=");
        serial_write(ahci_signature_name(sig));
        serial_write("\n");
        memset(adev, 0, sizeof(*adev));
        return;
    }

    if (blockdev_register(adev->name, 4u, index,
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
    serial_write(" sectors=");
    serial_write_hex(adev->sectors);
    serial_write("\n");

    ahci_count++;
}

static void ahci_enable_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t command = pci_read16(bus, dev, func, AHCI_PCI_COMMAND);
    command |= (uint16_t)(AHCI_PCI_CMD_IO | AHCI_PCI_CMD_MEM | AHCI_PCI_CMD_BUSMASTER);
    pci_write16(bus, dev, func, AHCI_PCI_COMMAND, command);
}

static void ahci_probe_controller(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t abar;
    ahci_hba_mem_t *hba;
    uint32_t implemented;
    uint32_t port;
    uint32_t reset_wait;

    ahci_enable_pci(bus, dev, func);
    abar = pci_read32(bus, dev, func, PCI_OFFSET_BAR5) & 0xFFFFFFF0u;
    if (abar == 0u) return;

    hba = (ahci_hba_mem_t *)(uintptr_t)abar;
    hba->ghc |= AHCI_GHC_AE;
    hba->ghc |= AHCI_GHC_HR;
    for (reset_wait = 0; reset_wait < AHCI_WAIT_LIMIT; reset_wait++) {
        if ((hba->ghc & AHCI_GHC_HR) == 0u) break;
    }
    hba->ghc |= AHCI_GHC_AE;
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
    memset(ahci_cmd_lists, 0, sizeof(ahci_cmd_lists));
    memset(ahci_fis_buffers, 0, sizeof(ahci_fis_buffers));
    memset(ahci_cmd_tables, 0, sizeof(ahci_cmd_tables));
    memset(ahci_identify_buffers, 0, sizeof(ahci_identify_buffers));
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

                if (pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_CLASS) == AHCI_PCI_CLASS_MASS_STORAGE &&
                    pci_read8((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_OFFSET_SUBCLASS) == AHCI_PCI_SUBCLASS_SATA &&
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
