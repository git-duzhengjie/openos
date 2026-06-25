#include "../include/uefi64.h"
#include "elf64.h"

#define EFI_LOAD_ERROR 0x8000000000000001ULL
#define UEFI64_MEMORY_MAP_BUFFER_DESCRIPTORS 256ULL
#define UEFI64_COM1_PORT 0x3F8u

typedef void (*kernel64_entry_fn_t)(const uefi64_handoff_info_t *);

static uefi64_handoff_info_t uefi64_handoff;
static efi_memory_descriptor64_t uefi64_memory_map_buffer[UEFI64_MEMORY_MAP_BUFFER_DESCRIPTORS];

static inline void uefi64_outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t uefi64_inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void uefi64_serial_init(void) {
    // Keep UEFI's DLAB and baud rate intact; only enable FIFO and aux outputs
    uefi64_outb(UEFI64_COM1_PORT + 2u, 0xC7u);
    uefi64_outb(UEFI64_COM1_PORT + 4u, 0x0Bu);
}

void uefi64_serial_putc(char c) {
    while ((uefi64_inb(UEFI64_COM1_PORT + 5u) & 0x20u) == 0) {
        __asm__ __volatile__("pause");
    }
    uefi64_outb(UEFI64_COM1_PORT, (uint8_t)c);
}

void uefi64_serial_write(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            uefi64_serial_putc('\r');
        }
        uefi64_serial_putc(*text++);
    }
}
static const efi_char16_t openos_uefi64_banner[] = {
    'O','p','e','n','O','S',' ','x','8','6','_','6','4',' ','U','E','F','I',' ','l','o','a','d','e','r',' ','r','e','a','d','y','\r','\n',0
};

static void uefi64_load_kernel(efi_system_table64_t *system_table,
                               efi_handle_t image_handle)
{
    efi_guid_t simple_file_system_guid = {
        0x964e5b22, 0x6459, 0x11d2,
        {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
    };
    efi_guid_t file_info_guid = {
        0x9576e92, 0x6d3f, 0x11d2,
        {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
    };
    efi_file_protocol_t *root = 0;
    efi_file_protocol_t *kernel_file = 0;
    efi_status_t status;
    uint8_t *kernel_buffer = 0;
    uint64_t file_size = 0;
    uint64_t buffer_size = 0;
    uint8_t info_buffer[256];
    uint64_t info_size = sizeof(info_buffer);
    efi_file_info_t *file_info = 0;

    /* Open ESP root */
    status = system_table->boot_services->handle_protocol(
        image_handle, &simple_file_system_guid, (void **)&root);
    if (status != EFI_SUCCESS || !root) {
        uefi64_serial_write("[UEFI] ERROR: Cannot open ESP filesystem\n");
        return;
    }
    uefi64_serial_write("[UEFI] ESP filesystem opened\n");

    /* Open kernel file */
    status = root->open(root, &kernel_file, (efi_char16_t *)L"\\kernel64.elf",
                        0x00000001 /* Read */, 0);
    if (status != EFI_SUCCESS || !kernel_file) {
        uefi64_serial_write("[UEFI] ERROR: Cannot open kernel64.elf\n");
        return;
    }
    uefi64_serial_write("[UEFI] kernel64.elf opened\n");

    /* Get file size */
    status = kernel_file->get_info(kernel_file, &file_info_guid, &info_size, info_buffer);
    if (status != EFI_SUCCESS) {
        uefi64_serial_write("[UEFI] ERROR: Cannot get file info\n");
        return;
    }
    file_info = (efi_file_info_t *)info_buffer;
    file_size = file_info->file_size;
    uefi64_serial_write("[UEFI] kernel64.elf size: ");
    uefi64_serial_putc('0' + (char)((file_size >> 20) / 10u));
    uefi64_serial_putc('0' + (char)((file_size >> 20) % 10u));
    uefi64_serial_write(" MiB\n");

    /* Allocate buffer */
    buffer_size = file_size + 4096;
    status = system_table->boot_services->allocate_pool(
        2 /* LoaderData */, buffer_size, (void **)&kernel_buffer);
    if (status != EFI_SUCCESS || !kernel_buffer) {
        uefi64_serial_write("[UEFI] ERROR: Cannot allocate kernel buffer\n");
        return;
    }

    /* Read kernel */
    buffer_size = file_size;
    status = kernel_file->read(kernel_file, &buffer_size, kernel_buffer);
    if (status != EFI_SUCCESS || buffer_size != file_size) {
        uefi64_serial_write("[UEFI] ERROR: Cannot read kernel64.elf\n");
        return;
    }
    uefi64_serial_write("[UEFI] kernel64.elf loaded to memory\n");

    /* Load ELF64 segments */
    status = elf64_load_segments(system_table, kernel_buffer, file_size);
    if (status != EFI_SUCCESS) {
        uefi64_serial_write("[UEFI] ERROR: ELF64 segment load failed\n");
        return;
    }

    /* Get entry point */
    uefi64_handoff.kernel_entry = elf64_get_entry(kernel_buffer);
    uefi64_serial_write("[UEFI] kernel entry at: ");
    uefi64_serial_write_hex64(uefi64_handoff.kernel_entry);
    uefi64_serial_write("\n");

    /* Cleanup */
    system_table->boot_services->free_pool(kernel_buffer);
    kernel_file->close(kernel_file);
    root->close(root);
}

static void uefi64_setup_framebuffer(efi_system_table64_t *system_table)
{
    efi_guid_t gop_guid = {
        0x9042a9de, 0x23dc, 0x4a38,
        {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
    };
    efi_gop_t *gop = 0;
    efi_status_t status;
    efi_gop_mode_info_t *mode_info = 0;

    status = system_table->boot_services->locate_protocol(&gop_guid, 0, (void **)&gop);
    if (status != EFI_SUCCESS || !gop) {
        uefi64_serial_write("[UEFI] No GOP framebuffer found\n");
        return;
    }

    mode_info = gop->mode->info;
    uefi64_handoff.framebuffer.base = gop->mode->frame_buffer_base;
    uefi64_handoff.framebuffer.size = gop->mode->frame_buffer_size;
    uefi64_handoff.framebuffer.width = mode_info->horizontal_resolution;
    uefi64_handoff.framebuffer.height = mode_info->vertical_resolution;
    uefi64_handoff.framebuffer.pitch = mode_info->pixels_per_scan_line * 4;
    uefi64_handoff.framebuffer.bpp = 32;

    uefi64_serial_write("[UEFI] GOP framebuffer: ");
    uefi64_serial_putc('0' + (char)(uefi64_handoff.framebuffer.width / 100u));
    uefi64_serial_putc('0' + (char)((uefi64_handoff.framebuffer.width / 10u) % 10u));
    uefi64_serial_putc('0' + (char)(uefi64_handoff.framebuffer.width % 10u));
    uefi64_serial_write("x");
    uefi64_serial_putc('0' + (char)(uefi64_handoff.framebuffer.height / 100u));
    uefi64_serial_putc('0' + (char)((uefi64_handoff.framebuffer.height / 10u) % 10u));
    uefi64_serial_putc('0' + (char)(uefi64_handoff.framebuffer.height % 10u));
    uefi64_serial_write(" @ ");
    uefi64_serial_write_hex64(uefi64_handoff.framebuffer.base);
    uefi64_serial_write("\n");
}

void uefi64_serial_write_hex64(uint64_t value)
{
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        if (nibble < 10) {
            uefi64_serial_putc('0' + nibble);
        } else {
            uefi64_serial_putc('A' + (nibble - 10));
        }
    }
}

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

static void __attribute__((naked)) uefi64_jump_to_kernel(const uefi64_handoff_info_t *handoff)
{
    __asm__ __volatile__(
        "cli\n"
        "movq %[handoff], %%rdi\n"
        "movq %[entry], %%rax\n"
        "jmpq *%%rax\n"
        :
        : [handoff] "r"(handoff), [entry] "m"(handoff->kernel_entry)
        : "memory"
    );
}

efi_status_t EFIAPI efi_main(efi_handle_t image_handle, efi_system_table64_t *system_table) {
    efi_status_t status;

    if (system_table == 0) {
        return EFI_LOAD_ERROR;
    }

    uefi64_serial_init();
    uefi64_serial_write("[UEFI] OpenOS x86_64 UEFI loader started\n");

    uefi64_zero(&uefi64_handoff, sizeof(uefi64_handoff));
    uefi64_handoff.magic = OPENOS_UEFI64_HANDOFF_MAGIC;
    uefi64_handoff.version = OPENOS_UEFI64_HANDOFF_VERSION;
    uefi64_handoff.image_handle = image_handle;
    uefi64_handoff.system_table = system_table;
    uefi64_handoff.kernel_entry = 0;

    /* Setup framebuffer first */
    uefi64_setup_framebuffer(system_table);

    /* Load kernel */
    uefi64_load_kernel(system_table, image_handle);
    if (uefi64_handoff.kernel_entry == 0) {
        uefi64_serial_write("[UEFI] ERROR: Kernel load failed, halting\n");
        for (;;) {
            __asm__ __volatile__("hlt");
        }
    }

    /* Capture memory map */
    uefi64_capture_memory_map(system_table);
    uefi64_console_write(system_table, openos_uefi64_banner);
    uefi64_serial_write("[UEFI] Memory map captured: ");
    uefi64_serial_putc('0' + (char)(uefi64_handoff.memory_descriptor_count / 10u));
    uefi64_serial_putc('0' + (char)(uefi64_handoff.memory_descriptor_count % 10u));
    uefi64_serial_write(" descriptors\n");

    /* Exit boot services */
    uefi64_serial_write("[UEFI] Exiting boot services...\n");
    status = system_table->boot_services->exit_boot_services(
        image_handle, uefi64_handoff.memory_map_key);
    if (status != EFI_SUCCESS) {
        uefi64_serial_write("[UEFI] WARNING: ExitBootServices failed, retrying...\n");
        uefi64_capture_memory_map(system_table);
        status = system_table->boot_services->exit_boot_services(
            image_handle, uefi64_handoff.memory_map_key);
        if (status != EFI_SUCCESS) {
            uefi64_serial_write("[UEFI] ERROR: ExitBootServices failed permanently\n");
            for (;;) {
                __asm__ __volatile__("hlt");
            }
        }
    }
    uefi64_serial_write("[UEFI] Boot services exited, jumping to kernel\n");

    /* Jump to kernel */
    uefi64_jump_to_kernel(&uefi64_handoff);

    /* Should never reach here */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
