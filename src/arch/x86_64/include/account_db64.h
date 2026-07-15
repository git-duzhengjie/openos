/*
 * account_db64.h — M6.11.3: account database parser
 *
 * Parses the classic UNIX account files shipped in the initrd:
 *   /etc/passwd   name:passwd:uid:gid:gecos:home:shell
 *   /etc/group    name:passwd:gid:members
 *   /etc/shadow   name:hash:...    (hash column = "sha256$<hex>")
 *
 * Pure parsing only; authentication policy lives in login64.c.
 * All lookups scan the in-memory initrd images and copy fields into
 * caller-provided fixed buffers (no dynamic allocation).
 */
#ifndef OPENOS_ARCH_X86_64_ACCOUNT_DB64_H
#define OPENOS_ARCH_X86_64_ACCOUNT_DB64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_ACCT_NAME_MAX   32u
#define OPENOS_X86_64_ACCT_PATH_MAX   64u
#define OPENOS_X86_64_ACCT_HASH_MAX   80u  /* "sha256$" + 64 hex + NUL slack */

/* One resolved /etc/passwd entry. */
typedef struct x86_64_passwd_entry {
    char     name[OPENOS_X86_64_ACCT_NAME_MAX];
    uint32_t uid;
    uint32_t gid;
    char     home[OPENOS_X86_64_ACCT_PATH_MAX];
    char     shell[OPENOS_X86_64_ACCT_PATH_MAX];
} x86_64_passwd_entry_t;

/* One resolved /etc/shadow entry (hash column only). */
typedef struct x86_64_shadow_entry {
    char name[OPENOS_X86_64_ACCT_NAME_MAX];
    char hash[OPENOS_X86_64_ACCT_HASH_MAX]; /* e.g. "sha256$<64 hex>" */
} x86_64_shadow_entry_t;

/*
 * Look up an account by name in /etc/passwd.
 * Returns 0 on success (out filled), negative on not-found / parse error.
 */
int arch_x86_64_passwd_lookup(const char *name, x86_64_passwd_entry_t *out);

/*
 * Look up the shadow hash for an account by name in /etc/shadow.
 * Returns 0 on success (out filled), negative on not-found / parse error.
 */
int arch_x86_64_shadow_lookup(const char *name, x86_64_shadow_entry_t *out);

#endif /* OPENOS_ARCH_X86_64_ACCOUNT_DB64_H */
