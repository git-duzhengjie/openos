/*
 * login64.h — M6.11.3: login / session authentication
 *
 * Ties together the credential model (M6.11.1) and the account database
 * (M6.11.2 + account_db64) into a full authenticate -> drop-privilege ->
 * start-session flow, all in the kernel for now (a user-space login(1)
 * can later call the same policy via a syscall).
 *
 * Password verification recomputes SHA256(password) and compares it to the
 * 'sha256$<hex>' column stored in /etc/shadow.
 */
#ifndef OPENOS_ARCH_X86_64_LOGIN64_H
#define OPENOS_ARCH_X86_64_LOGIN64_H

#include <stdint.h>

#include "account_db64.h"

/* Authentication result codes. */
typedef enum x86_64_login_result {
    X86_64_LOGIN_OK          = 0,  /* credentials valid */
    X86_64_LOGIN_NO_USER     = -1, /* no such account in /etc/passwd */
    X86_64_LOGIN_NO_SHADOW   = -2, /* account has no /etc/shadow entry */
    X86_64_LOGIN_BAD_HASH    = -3, /* shadow hash malformed / unsupported */
    X86_64_LOGIN_BAD_PASSWORD = -4, /* password mismatch */
    X86_64_LOGIN_EINVAL      = -5, /* bad arguments */
} x86_64_login_result_t;

/*
 * Verify (name, password) against the account database.
 * On success (return X86_64_LOGIN_OK) the resolved passwd entry is copied
 * into *pw (may be NULL if the caller does not need it).
 * Does NOT mutate any process state.
 */
x86_64_login_result_t arch_x86_64_login_authenticate(
    const char *name, const char *password, x86_64_passwd_entry_t *pw);

/*
 * Establish a session for an already-authenticated passwd entry on the
 * current PCB: become a session/group leader (setsid) then drop to the
 * account's gid/uid (real+effective+saved). Order matters — setsid first
 * (needs no privilege here) then setgid before setuid so we don't lose the
 * ability to change gid after dropping uid.
 * Returns X86_64_LOGIN_OK or an error code.
 */
x86_64_login_result_t arch_x86_64_login_start_session(
    const x86_64_passwd_entry_t *pw);

/*
 * Convenience: authenticate then, on success, start the session.
 * Combines the two calls above; returns the first failure encountered.
 */
x86_64_login_result_t arch_x86_64_login(
    const char *name, const char *password, x86_64_passwd_entry_t *pw);

/* Human-readable name for a result code (for diagnostics). */
const char *arch_x86_64_login_result_str(x86_64_login_result_t r);

#endif /* OPENOS_ARCH_X86_64_LOGIN64_H */
