#include "../include/uefi64.h"

#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define UEFI64_MEMORY_MAP_BUFFER_DESCRIPTORS 256ULL

static uefi64_handoff_info_t uefi64_handoff;
static efi_memory_descriptor64_t uefi64_memory_map_buffer[UEFI64_MEMORY_MAP_BUFFER_DESCRIPTORS];
static const efi_char16_t openos_uefi64_banner[] = {
    'O','p','e','n','O','S',' ','x','8','6','_','6','4',' ','U','E','F','I',' ','l','o','a','d','e','r',' ','r','e','a','d','y','\r','\n',0
};

static void uefi64_zero(void *ptr, uint64_t size) {
    uint8_t *out = (uint8_t *)ptr;
    uint64_t i;
    for (i = 0; i < size; ++i) {
        out[i] = 0;
    }
}

static void uefi64_console_write(efi_system_table64_t *system_table, const efi_char16_t *text) {
    if (system_table != 0 && system_table->con_out != 0 && system_table->con_out->output_string != 0) {
        (void)system_table->con_out->output_string(system_table->con_out, text);
    }
}

const uefi64_handoff_info_t *uefi64_get_handoff_info(void) {
    return &uefi64_handoff;
}

static void uefi64_capture_memory_map(efi_system_table64_t *system_table) {
    uint64_t memory_map_size = sizeof(uefi64_memory_map_buffer);
    uint64_t map_key = 0;
    uint64_t descriptor_size = sizeof(efi_memory_descriptor64_t);
    uint32_t descriptor_version = 0;
    efi_status_t status;
    uint64_t count;
    uint64_t i;
    uint8_t *cursor;

    if (system_table == 0 || system_table->boot_services == 0 || system_table->boot_services->get_memory_map == 0) {
        return;
    }

    status = system_table->boot_services->get_memory_map(&memory_map_size,
                                                         &uefi64_memory_map_buffer[0],
                                                         &map_key,
                                                         &descriptor_size,
                                                         &descriptor_version);
    if (status != EFI_SUCCESS || descriptor_size == 0) {
        uefi64_handoff.memory_descriptor_count = 0;
        return;
    }

    count = memory_map_size / descriptor_size;
    if (count > OPENOS_UEFI64_MAX_MEMORY_DESCRIPTORS) {
        count = OPENOS_UEFI64_MAX_MEMORY_DESCRIPTORS;
    }

    cursor = (uint8_t *)&uefi64_memory_map_buffer[0];
    for (i = 0; i < count; ++i) {
        const efi_memory_descriptor64_t *desc = (const efi_memory_descriptor64_t *)(const void *)cursor;
        uefi64_handoff.descriptors[i].type = desc->type;
        uefi64_handoff.descriptors[i].physical_start = desc->physical_start;
        uefi64_handoff.descriptors[i].virtual_start = desc->virtual_start;
        uefi64_handoff.descriptors[i].number_of_pages = desc->number_of_pages;
        uefi64_handoff.descriptors[i].attribute = desc->attribute;
        cursor += descriptor_size;
    }

    uefi64_handoff.memory_descriptor_count = count;
    uefi64_handoff.memory_descriptor_size = descriptor_size;
    uefi64_handoff.memory_map_key = map_key;
}

efi_status_t EFIAPI efi_main(efi_handle_t image_handle, efi_system_table64_t *system_table) {
    if (system_table == 0) {
        return EFI_LOAD_ERROR;
    }

    uefi64_zero(&uefi64_handoff, sizeof(uefi64_handoff));
    uefi64_handoff.magic = OPENOS_UEFI64_HANDOFF_MAGIC;
    uefi64_handoff.version = OPENOS_UEFI64_HANDOFF_VERSION;
    uefi64_handoff.image_handle = image_handle;
    uefi64_handoff.system_table = system_table;
    uefi64_handoff.kernel_entry = 0;
    uefi64_capture_memory_map(system_table);

    uefi64_console_write(system_table, openos_uefi64_banner);

    return EFI_SUCCESS;
}
