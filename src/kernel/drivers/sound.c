/* ============================================================
 * openos - Sound driver core
 *
 * Provides a common PCI sound-device registry plus a PC speaker
 * fallback beep. Real AC97/HDA PCM playback can build on this layer.
 * ============================================================ */

#include "sound.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "io.h"

#define PCI_BAR_IO_MASK  0xFFFFFFFCu
#define PCI_BAR_MEM_MASK 0xFFFFFFF0u
#define PIT_COMMAND      0x43
#define PIT_CHANNEL2     0x42
#define PC_SPEAKER_PORT  0x61
#define PIT_BASE_HZ      1193180u

static sound_device_t g_sound_devices[SOUND_MAX_DEVICES];
static sound_stats_t g_sound_stats;
static uint8_t g_sound_initialized;
static uint8_t g_pc_speaker_index = 0xFF;

static void sound_delay_ms(uint32_t ms) {
    volatile uint32_t loops;
    while (ms--) {
        loops = 12000;
        while (loops--) {
            __asm__ volatile("pause");
        }
    }
}

const char *sound_device_type_name(sound_device_type_t type) {
    switch (type) {
        case SOUND_DEVICE_AC97: return "AC97";
        case SOUND_DEVICE_HDA: return "HDA";
        case SOUND_DEVICE_AUDIO: return "audio";
        case SOUND_DEVICE_PC_SPEAKER: return "pc-speaker";
        default: return "unknown";
    }
}

static sound_device_type_t sound_type_from_subclass(uint8_t subclass, uint8_t prog_if) {
    if (subclass == SOUND_PCI_SUBCLASS_HDA) {
        return SOUND_DEVICE_HDA;
    }
    if (subclass == SOUND_PCI_SUBCLASS_AUDIO) {
        if (prog_if == 0x00) {
            return SOUND_DEVICE_AC97;
        }
        return SOUND_DEVICE_AUDIO;
    }
    return SOUND_DEVICE_UNKNOWN;
}

static void sound_clear_registry(void) {
    memset(g_sound_devices, 0, sizeof(g_sound_devices));
    g_sound_stats.device_count = 0;
    g_sound_stats.ac97_count = 0;
    g_sound_stats.hda_count = 0;
    g_sound_stats.audio_count = 0;
    g_sound_stats.pc_speaker_count = 0;
    g_pc_speaker_index = 0xFF;
}

static void sound_count_type(sound_device_type_t type) {
    if (type == SOUND_DEVICE_AC97) g_sound_stats.ac97_count++;
    else if (type == SOUND_DEVICE_HDA) g_sound_stats.hda_count++;
    else if (type == SOUND_DEVICE_AUDIO) g_sound_stats.audio_count++;
    else if (type == SOUND_DEVICE_PC_SPEAKER) g_sound_stats.pc_speaker_count++;
}

static void sound_read_bar(sound_device_t *device, uint32_t bar) {
    if (bar & 1u) {
        device->io_base = (uint16_t)(bar & PCI_BAR_IO_MASK);
        device->mem_base = 0;
    } else {
        device->io_base = 0;
        device->mem_base = bar & PCI_BAR_MEM_MASK;
    }
}

static void sound_register_pci_device(uint8_t bus, uint8_t dev, uint8_t func, uint32_t class_reg) {
    sound_device_t *device;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar0;
    uint32_t slot;

    if (g_sound_stats.device_count >= SOUND_MAX_DEVICES) {
        return;
    }

    subclass = (uint8_t)(class_reg >> 16);
    prog_if = (uint8_t)(class_reg >> 8);
    slot = g_sound_stats.device_count;
    device = &g_sound_devices[slot];
    memset(device, 0, sizeof(*device));
    device->used = 1;
    device->type = sound_type_from_subclass(subclass, prog_if);
    device->bus = bus;
    device->dev = dev;
    device->func = func;
    device->irq = pci_read8(bus, dev, func, PCI_OFFSET_INTLINE);
    device->vendor_id = pci_read16(bus, dev, func, PCI_OFFSET_VENDOR);
    device->device_id = pci_read16(bus, dev, func, PCI_OFFSET_DEVICE);
    device->pci_class = class_reg;
    bar0 = pci_read32(bus, dev, func, PCI_OFFSET_BAR0);
    sound_read_bar(device, bar0);
    device->playback_ready = 0;

    sound_count_type(device->type);
    g_sound_stats.device_count++;
}

static void sound_register_pc_speaker(void) {
    sound_device_t *device;
    uint32_t slot;
    if (g_sound_stats.device_count >= SOUND_MAX_DEVICES) {
        return;
    }
    slot = g_sound_stats.device_count;
    device = &g_sound_devices[slot];
    memset(device, 0, sizeof(*device));
    device->used = 1;
    device->type = SOUND_DEVICE_PC_SPEAKER;
    device->playback_ready = 1;
    g_pc_speaker_index = (uint8_t)slot;
    g_sound_stats.device_count++;
    sound_count_type(device->type);
}

void sound_rescan(void) {
    g_sound_stats.scans++;
    sound_clear_registry();
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint8_t header;
            uint8_t func_count;
            uint16_t vendor = pci_read16((uint8_t)bus, dev, 0, PCI_OFFSET_VENDOR);
            if (vendor == PCI_VENDOR_INVALID) {
                continue;
            }
            header = pci_read8((uint8_t)bus, dev, 0, PCI_OFFSET_HEADER);
            func_count = (header & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < func_count; func++) {
                uint32_t class_reg;
                uint8_t class_code;
                uint8_t subclass;
                vendor = pci_read16((uint8_t)bus, dev, func, PCI_OFFSET_VENDOR);
                if (vendor == PCI_VENDOR_INVALID) {
                    continue;
                }
                class_reg = pci_read32((uint8_t)bus, dev, func, PCI_OFFSET_CLASS);
                class_code = (uint8_t)(class_reg >> 24);
                subclass = (uint8_t)(class_reg >> 16);
                if (class_code == SOUND_PCI_CLASS_MULTIMEDIA &&
                    (subclass == SOUND_PCI_SUBCLASS_AUDIO || subclass == SOUND_PCI_SUBCLASS_HDA || subclass == SOUND_PCI_SUBCLASS_OTHER)) {
                    sound_register_pci_device((uint8_t)bus, dev, func, class_reg);
                }
            }
        }
    }
    sound_register_pc_speaker();
}

void sound_init(void) {
    memset(&g_sound_stats, 0, sizeof(g_sound_stats));
    sound_clear_registry();
    g_sound_initialized = 1;
    sound_rescan();
    serial_write("[OK] SOUND devices=");
    serial_write_hex(g_sound_stats.device_count);
    serial_write(" hda=");
    serial_write_hex(g_sound_stats.hda_count);
    serial_write(" ac97=");
    serial_write_hex(g_sound_stats.ac97_count);
    serial_write("\n");
}

uint32_t sound_device_count(void) {
    return g_sound_stats.device_count;
}

const sound_device_t *sound_get_device(uint32_t index) {
    if (index >= g_sound_stats.device_count || index >= SOUND_MAX_DEVICES) {
        return 0;
    }
    return &g_sound_devices[index];
}

const sound_stats_t *sound_get_stats(void) {
    return &g_sound_stats;
}

void sound_beep(uint32_t frequency_hz, uint32_t duration_ms) {
    uint32_t divisor;
    uint8_t speaker;
    if (frequency_hz < 20 || frequency_hz > 20000) {
        frequency_hz = 880;
    }
    if (duration_ms == 0 || duration_ms > 5000) {
        duration_ms = 120;
    }
    divisor = PIT_BASE_HZ / frequency_hz;
    if (divisor == 0 || divisor > 0xFFFFu) {
        divisor = PIT_BASE_HZ / 880u;
    }

    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF));

    speaker = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, (uint8_t)(speaker | 0x03));
    sound_delay_ms(duration_ms);
    outb(PC_SPEAKER_PORT, (uint8_t)(inb(PC_SPEAKER_PORT) & 0xFC));

    g_sound_stats.beep_count++;
    if (g_pc_speaker_index != 0xFF) {
        g_sound_devices[g_pc_speaker_index].beep_count++;
    }
}

void sound_print_info(void) {
    if (!g_sound_initialized) {
        serial_write("[SOUND] driver not initialized\n");
        return;
    }
    serial_write("[SOUND] scans="); serial_write_hex(g_sound_stats.scans);
    serial_write(" devices="); serial_write_hex(g_sound_stats.device_count);
    serial_write(" ac97="); serial_write_hex(g_sound_stats.ac97_count);
    serial_write(" hda="); serial_write_hex(g_sound_stats.hda_count);
    serial_write(" audio="); serial_write_hex(g_sound_stats.audio_count);
    serial_write(" pcspk="); serial_write_hex(g_sound_stats.pc_speaker_count);
    serial_write(" beeps="); serial_write_hex(g_sound_stats.beep_count);
    serial_write("\n");
    for (uint32_t i = 0; i < g_sound_stats.device_count; i++) {
        const sound_device_t *d = &g_sound_devices[i];
        serial_write("[SOUND] dev#"); serial_write_hex(i);
        serial_write(" type="); serial_write(sound_device_type_name(d->type));
        serial_write(" pci="); serial_write_hex(d->bus); serial_write(":");
        serial_write_hex(d->dev); serial_write("."); serial_write_hex(d->func);
        serial_write(" vendor="); serial_write_hex(d->vendor_id);
        serial_write(" device="); serial_write_hex(d->device_id);
        serial_write(" irq="); serial_write_hex(d->irq);
        serial_write(" io="); serial_write_hex(d->io_base);
        serial_write(" mem="); serial_write_hex(d->mem_base);
        serial_write(" ready="); serial_write_hex(d->playback_ready);
        serial_write(" beeps="); serial_write_hex(d->beep_count);
        serial_write("\n");
    }
}
