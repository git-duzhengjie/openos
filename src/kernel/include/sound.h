#ifndef SOUND_H
#define SOUND_H

#include "types.h"

#define SOUND_MAX_DEVICES 8

#define SOUND_PCI_CLASS_MULTIMEDIA 0x04
#define SOUND_PCI_SUBCLASS_VIDEO   0x00
#define SOUND_PCI_SUBCLASS_AUDIO   0x01
#define SOUND_PCI_SUBCLASS_TELEPHONY 0x02
#define SOUND_PCI_SUBCLASS_HDA     0x03
#define SOUND_PCI_SUBCLASS_OTHER   0x80

typedef enum sound_device_type {
    SOUND_DEVICE_UNKNOWN = 0,
    SOUND_DEVICE_AC97,
    SOUND_DEVICE_HDA,
    SOUND_DEVICE_AUDIO,
    SOUND_DEVICE_PC_SPEAKER
} sound_device_type_t;

typedef struct sound_device {
    uint8_t used;
    sound_device_type_t type;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t irq;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t io_base;
    uint32_t mem_base;
    uint32_t pci_class;
    uint8_t playback_ready;
    uint32_t beep_count;
} sound_device_t;

typedef struct sound_stats {
    uint32_t scans;
    uint32_t device_count;
    uint32_t ac97_count;
    uint32_t hda_count;
    uint32_t audio_count;
    uint32_t pc_speaker_count;
    uint32_t beep_count;
} sound_stats_t;

void sound_init(void);
void sound_rescan(void);
uint32_t sound_device_count(void);
const sound_device_t *sound_get_device(uint32_t index);
const sound_stats_t *sound_get_stats(void);
const char *sound_device_type_name(sound_device_type_t type);
void sound_print_info(void);
void sound_beep(uint32_t frequency_hz, uint32_t duration_ms);

#endif /* SOUND_H */
