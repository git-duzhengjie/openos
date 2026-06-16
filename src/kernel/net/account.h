#ifndef OPENOS_ACCOUNT_H
#define OPENOS_ACCOUNT_H

#include "types.h"
#include "discovery.h"

#define ACCOUNT_ID_MAX 32
#define ACCOUNT_NAME_MAX 32
#define ACCOUNT_PAIRING_CODE_MAX 32
#define ACCOUNT_KEY_HEX_LEN 16
#define ACCOUNT_E2E_PREFIX "E2E1:"

typedef struct account_status {
    int configured;
    int e2e_enabled;
    char account_id[ACCOUNT_ID_MAX];
    char display_name[ACCOUNT_NAME_MAX];
    char key_fingerprint[ACCOUNT_KEY_HEX_LEN + 1];
} account_status_t;

void account_init(void);
int account_configure(const char *account_id, const char *display_name, const char *pairing_code);
int account_set_pairing_code(const char *pairing_code);
int account_rotate_key(void);
int account_e2e_enabled(void);
void account_get_status(account_status_t *out);
uint32_t account_group_key(void);
int account_encrypt_field(const char *plain, char *out, uint32_t out_size);
int account_decrypt_field(const char *encoded, char *out, uint32_t out_size);
void account_print_info(void);

#endif /* OPENOS_ACCOUNT_H */
