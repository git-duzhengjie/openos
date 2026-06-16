#include "../include/uefi64.h"

#define EFI_SUCCESS 0ULL
#define EFI_LOAD_ERROR 0x8000000000000001ULL

static uefi64_handoff_info_t uefi64_handoff;
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

    uefi64_console_write(system_table, openos_uefi64_banner);

    return EFI_SUCCESS;
}
