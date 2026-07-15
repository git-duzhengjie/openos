/*
 * login64.c — M6.11.3: login / session authentication
 *
 * authenticate: /etc/passwd lookup + /etc/shadow SHA256 password check.
 * start_session: setsid() then drop gid/uid (real+eff+saved) on current PCB.
 *
 * The stored hash column is "sha256$<64 lowercase hex>". We recompute
 * SHA256(password), hex-encode it, and compare constant-length against the
 * stored hex (byte-wise; timing is not a concern for a boot-time console).
 */
#include "../include/login64.h"
#include "../include/account_db64.h"
#include "../include/proc64.h"

#include "sha256.h"

#include <stdint.h>
#include <stddef.h>

/* ---- local helpers ---- */

static size_t lg_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static const char lg_hexdigits[] = "0123456789abcdef";

/* Hex-encode digest[0..len) into out (out must hold 2*len+1 bytes). */
static void lg_hex_encode(const uint8_t *digest, size_t len, char *out)
{
    size_t i;
    for (i = 0; i < len; i++) {
        out[2 * i]     = lg_hexdigits[(digest[i] >> 4) & 0xF];
        out[2 * i + 1] = lg_hexdigits[digest[i] & 0xF];
    }
    out[2 * len] = '\0';
}

/* Compare two NUL-terminated strings for exact equality. */
static int lg_streq(const char *a, const char *b)
{
    size_t i = 0;
    for (; a[i] && b[i]; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == b[i];
}

/* Check that s begins with prefix; return pointer past prefix, or NULL. */
static const char *lg_after_prefix(const char *s, const char *prefix)
{
    size_t i = 0;
    for (; prefix[i]; i++) {
        if (s[i] != prefix[i]) return NULL;
    }
    return s + i;
}

x86_64_login_result_t arch_x86_64_login_authenticate(
    const char *name, const char *password, x86_64_passwd_entry_t *pw)
{
    if (!name || !password) return X86_64_LOGIN_EINVAL;

    x86_64_passwd_entry_t pe;
    if (arch_x86_64_passwd_lookup(name, &pe) != 0) {
        return X86_64_LOGIN_NO_USER;
    }

    x86_64_shadow_entry_t se;
    if (arch_x86_64_shadow_lookup(name, &se) != 0) {
        return X86_64_LOGIN_NO_SHADOW;
    }

    /* Only "sha256$<hex>" is supported. */
    const char *stored_hex = lg_after_prefix(se.hash, "sha256$");
    if (!stored_hex) {
        return X86_64_LOGIN_BAD_HASH;
    }
    if (lg_strlen(stored_hex) != (size_t)(SHA256_DIGEST_SIZE * 2)) {
        return X86_64_LOGIN_BAD_HASH;
    }

    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256((const uint8_t *)password, lg_strlen(password), digest);

    char computed_hex[SHA256_DIGEST_SIZE * 2 + 1];
    lg_hex_encode(digest, SHA256_DIGEST_SIZE, computed_hex);

    if (!lg_streq(computed_hex, stored_hex)) {
        return X86_64_LOGIN_BAD_PASSWORD;
    }

    if (pw) *pw = pe;
    return X86_64_LOGIN_OK;
}

x86_64_login_result_t arch_x86_64_login_start_session(
    const x86_64_passwd_entry_t *pw)
{
    if (!pw) return X86_64_LOGIN_EINVAL;

    /* Become session+group leader before dropping privilege. */
    (void)arch_x86_64_proc_setsid();

    /*
     * Drop gid first: once uid is non-root, setgid() would be denied by
     * POSIX rules, so we must lower the group while still privileged.
     */
    if (arch_x86_64_proc_setgid(pw->gid) != 0) {
        return X86_64_LOGIN_EINVAL;
    }
    if (arch_x86_64_proc_setuid(pw->uid) != 0) {
        return X86_64_LOGIN_EINVAL;
    }

    return X86_64_LOGIN_OK;
}

x86_64_login_result_t arch_x86_64_login(
    const char *name, const char *password, x86_64_passwd_entry_t *pw)
{
    x86_64_passwd_entry_t pe;
    x86_64_login_result_t r = arch_x86_64_login_authenticate(name, password, &pe);
    if (r != X86_64_LOGIN_OK) return r;

    r = arch_x86_64_login_start_session(&pe);
    if (r != X86_64_LOGIN_OK) return r;

    if (pw) *pw = pe;
    return X86_64_LOGIN_OK;
}

const char *arch_x86_64_login_result_str(x86_64_login_result_t r)
{
    switch (r) {
    case X86_64_LOGIN_OK:           return "OK";
    case X86_64_LOGIN_NO_USER:      return "NO_USER";
    case X86_64_LOGIN_NO_SHADOW:    return "NO_SHADOW";
    case X86_64_LOGIN_BAD_HASH:     return "BAD_HASH";
    case X86_64_LOGIN_BAD_PASSWORD: return "BAD_PASSWORD";
    case X86_64_LOGIN_EINVAL:       return "EINVAL";
    default:                        return "UNKNOWN";
    }
}
