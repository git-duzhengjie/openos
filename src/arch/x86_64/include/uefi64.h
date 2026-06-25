#ifndef OPENOS_ARCH_X86_64_UEFI64_H
#define OPENOS_ARCH_X86_64_UEFI64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_UEFI64_HANDOFF_MAGIC 0x4F53454649554546ULL
#define OPENOS_UEFI64_HANDOFF_VERSION 1ULL
#define OPENOS_UEFI64_MAX_MEMORY_DESCRIPTORS 128U

#define EFIAPI __attribute__((ms_abi))

#define EFI_SUCCESS 0ULL
#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL

typedef void *efi_handle_t;
typedef uint64_t efi_status_t;
typedef uint16_t efi_char16_t;

typedef struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} efi_guid_t;

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

/* 文件系统相关前向声明 */
typedef struct efi_file_protocol efi_file_protocol_t;
typedef struct efi_simple_file_system_protocol efi_simple_file_system_protocol_t;

typedef efi_status_t (EFIAPI *efi_text_string64_t)(efi_simple_text_output_protocol64_t *self,
                                                   const efi_char16_t *string);
typedef efi_status_t (EFIAPI *efi_get_memory_map64_t)(uint64_t *memory_map_size,
                                                      efi_memory_descriptor64_t *memory_map,
                                                      uint64_t *map_key,
                                                      uint64_t *descriptor_size,
                                                      uint32_t *descriptor_version);
typedef efi_status_t (EFIAPI *efi_allocate_pages64_t)(uint32_t type,
                                                      uint32_t memory_type,
                                                      uint64_t pages,
                                                      uint64_t *memory);
typedef efi_status_t (EFIAPI *efi_free_pages64_t)(uint64_t memory,
                                                  uint64_t pages);
typedef efi_status_t (EFIAPI *efi_allocate_pool64_t)(uint32_t pool_type,
                                                     uint64_t size,
                                                     void **buffer);
typedef efi_status_t (EFIAPI *efi_free_pool64_t)(void *buffer);
typedef efi_status_t (EFIAPI *efi_handle_protocol64_t)(efi_handle_t handle,
                                                       efi_guid_t *protocol,
                                                       void **interface);
typedef efi_status_t (EFIAPI *efi_locate_protocol64_t)(efi_guid_t *protocol,
                                                       void *registration,
                                                       void **interface);
typedef efi_status_t (EFIAPI *efi_exit_boot_services64_t)(efi_handle_t image_handle,
                                                          uint64_t map_key);
typedef efi_status_t (EFIAPI *efi_file_delete_t)(efi_file_protocol_t *self);
typedef efi_status_t (EFIAPI *efi_file_get_position_t)(efi_file_protocol_t *self,
                                                       uint64_t *position);
typedef efi_status_t (EFIAPI *efi_file_set_position_t)(efi_file_protocol_t *self,
                                                       uint64_t position);
typedef efi_status_t (EFIAPI *efi_simple_file_system_open_volume_t)(
    efi_simple_file_system_protocol_t *self,
    efi_file_protocol_t **root);

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
    efi_allocate_pages64_t allocate_pages;
    efi_free_pages64_t free_pages;
    efi_get_memory_map64_t get_memory_map;
    void *set_mem;
    void *copy_mem;
    efi_allocate_pool64_t allocate_pool;
    efi_free_pool64_t free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_handle_protocol64_t handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    efi_locate_protocol64_t locate_protocol;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_exit_boot_services64_t exit_boot_services;
};

/* Simple File System Protocol */
typedef struct efi_file_protocol efi_file_protocol_t;
typedef struct efi_simple_file_system_protocol efi_simple_file_system_protocol_t;

typedef efi_status_t (EFIAPI *efi_file_open_t)(efi_file_protocol_t *self,
                                                efi_file_protocol_t **new_handle,
                                                efi_char16_t *file_name,
                                                uint64_t open_mode,
                                                uint64_t attributes);
typedef efi_status_t (EFIAPI *efi_file_close_t)(efi_file_protocol_t *self);
typedef efi_status_t (EFIAPI *efi_file_read_t)(efi_file_protocol_t *self,
                                               uint64_t *buffer_size,
                                               void *buffer);
typedef efi_status_t (EFIAPI *efi_file_write_t)(efi_file_protocol_t *self,
                                                uint64_t *buffer_size,
                                                void *buffer);
typedef efi_status_t (EFIAPI *efi_file_get_info_t)(efi_file_protocol_t *self,
                                                   efi_guid_t *information_type,
                                                   uint64_t *buffer_size,
                                                   void *buffer);
typedef efi_status_t (EFIAPI *efi_file_set_info_t)(efi_file_protocol_t *self,
                                                   efi_guid_t *information_type,
                                                   uint64_t buffer_size,
                                                   void *buffer);
typedef efi_status_t (EFIAPI *efi_file_flush_t)(efi_file_protocol_t *self);

struct efi_file_protocol {
    uint64_t revision;
    efi_file_open_t open;
    efi_file_close_t close;
    efi_file_delete_t del;
    efi_file_read_t read;
    efi_file_write_t write;
    efi_file_get_position_t get_position;
    efi_file_set_position_t set_position;
    efi_file_get_info_t get_info;
    efi_file_set_info_t set_info;
    efi_file_flush_t flush;
};

struct efi_simple_file_system_protocol {
    uint64_t revision;
    efi_simple_file_system_open_volume_t open_volume;
};

/* Graphics Output Protocol (GOP) */
typedef struct efi_gop_pixel_bitmask {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} efi_gop_pixel_bitmask_t;

typedef enum {
    EFI_GOP_PIXEL_RED_GREEN_BLUE_RESERVED_8_BIT_PER_COLOR,
    EFI_GOP_PIXEL_BLUE_GREEN_RED_RESERVED_8_BIT_PER_COLOR,
    EFI_GOP_PIXEL_BIT_MASK,
    EFI_GOP_PIXEL_BLT_ONLY,
    EFI_GOP_PIXEL_FORMAT_MAX
} efi_gop_pixel_format_t;

typedef struct efi_gop_mode_info {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    efi_gop_pixel_format_t pixel_format;
    efi_gop_pixel_bitmask_t pixel_information;
    uint32_t pixels_per_scan_line;
} efi_gop_mode_info_t;

typedef struct efi_gop_mode {
    uint32_t max_mode;
    uint32_t mode;
    efi_gop_mode_info_t *info;
    uint64_t size_of_info;
    uint64_t frame_buffer_base;
    uint64_t frame_buffer_size;
} efi_gop_mode_t;

typedef struct efi_gop {
    void *query_mode;
    void *set_mode;
    void *blt;
    efi_gop_mode_t *mode;
} efi_gop_t;

/* EFI File Info */
typedef struct efi_file_info {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    uint64_t create_time;
    uint64_t last_access_time;
    uint64_t modification_time;
    uint64_t attribute;
    efi_char16_t file_name[1];
} efi_file_info_t;

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
    uint64_t size;
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

/* 串口调试函数 */
void uefi64_serial_init(void);
void uefi64_serial_putc(char c);
void uefi64_serial_write(const char *str);
void uefi64_serial_write_hex64(uint64_t value);

#endif /* OPENOS_ARCH_X86_64_UEFI64_H */
