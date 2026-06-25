#ifndef OPENOS_ARCH_X86_64_UEFI64_H
#define OPENOS_ARCH_X86_64_UEFI64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_UEFI64_HANDOFF_MAGIC 0x4F53454649554546ULL
#define OPENOS_UEFI64_HANDOFF_VERSION 1ULL
#define OPENOS_UEFI64_MAX_MEMORY_DESCRIPTORS 128U

#define EFIAPI __attribute__((ms_abi))

#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL

typedef void *efi_handle_t;
typedef uint64_t efi_status_t;
typedef uint16_t efi_char16_t;

typedef struct efi_table_header64 {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header64_t;

typedef struct efi_simple_text_output_protocol64 efi_simple_text_output_protocol64_t;
typedef struct efi_system_table64 efi_system_table64_t;
typedef struct efi_boot_services64 efi_boot_services64_t;
typedef struct efi_memory_descriptor64 efi_memory_descriptor64_t;

typedef efi_status_t (EFIAPI *efi_text_string64_t)(efi_simple_text_output_protocol64_t *self,
                                                   const efi_char16_t *string);
typedef efi_status_t (EFIAPI *efi_get_memory_map64_t)(uint64_t *memory_map_size,
                                                      efi_memory_descriptor64_t *memory_map,
                                                      uint64_t *map_key,
                                                      uint64_t *descriptor_size,
                                                      uint32_t *descriptor_version);

struct efi_simple_text_output_protocol64 {
    void *reset;
    efi_text_string64_t output_string;
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
};

struct efi_memory_descriptor64 {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

struct efi_boot_services64 {
    efi_table_header64_t hdr;
    void *raise_tpl;
    void *restore_tpl;
    void *allocate_pages;
    void *free_pages;
    efi_get_memory_map64_t get_memory_map;
};

struct efi_system_table64 {
    efi_table_header64_t hdr;
    efi_char16_t *firmware_vendor;
    uint32_t firmware_revision;
    efi_handle_t console_in_handle;
    void *con_in;
    efi_handle_t console_out_handle;
    efi_simple_text_output_protocol64_t *con_out;
    efi_handle_t standard_error_handle;
    efi_simple_text_output_protocol64_t *std_err;
    void *runtime_services;
    efi_boot_services64_t *boot_services;
    uint64_t number_of_table_entries;
    void *configuration_table;
};

typedef struct uefi64_framebuffer_info {
    x86_64_phys_addr_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t pixel_format;
} uefi64_framebuffer_info_t;

typedef struct uefi64_memory_descriptor_info {
    uint32_t type;
    x86_64_phys_addr_t physical_start;
    x86_64_virt_addr_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} uefi64_memory_descriptor_info_t;

typedef struct uefi64_handoff_info {
    uint64_t magic;
    uint64_t version;
    efi_handle_t image_handle;
    efi_system_table64_t *system_table;
    uefi64_framebuffer_info_t framebuffer;
    uint64_t memory_descriptor_count;
    uint64_t memory_descriptor_size;
    uint64_t memory_map_key;
    uefi64_memory_descriptor_info_t descriptors[OPENOS_UEFI64_MAX_MEMORY_DESCRIPTORS];
    x86_64_entry_t kernel_entry;
} uefi64_handoff_info_t;

efi_status_t EFIAPI efi_main(efi_handle_t image_handle, efi_system_table64_t *system_table);
const uefi64_handoff_info_t *uefi64_get_handoff_info(void);

#endif /* OPENOS_ARCH_X86_64_UEFI64_H */
