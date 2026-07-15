#ifndef OPENOS_ARCH_X86_64_LOGIN_SELFTEST64_H
#define OPENOS_ARCH_X86_64_LOGIN_SELFTEST64_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * M6.11.3 login/session selftest. Exercises the full authentication and
 * session-establishment path against the initrd account database:
 *
 *   - authenticate("root", "root")     -> OK, resolves uid 0
 *   - authenticate("openos", "openos") -> OK, resolves the unprivileged uid
 *   - wrong password                   -> BAD_PASSWORD
 *   - unknown account                  -> NO_USER
 *   - start_session() drops the current PCB to the account's uid/gid and
 *     makes it a session leader (sid == pid)
 *
 * The test snapshots the current PCB credentials and session id, drives the
 * login API, verifies every outcome, then restores the pristine root
 * identity so the live kernel PCB (slot 0) is left untouched. Returns true
 * on PASS.
 */
bool arch_x86_64_login_selftest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_ARCH_X86_64_LOGIN_SELFTEST64_H */
