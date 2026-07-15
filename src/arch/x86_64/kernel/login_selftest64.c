/*
 * login_selftest64.c — M6.11.3 login / session selftest
 *
 * Drives the authenticate -> drop-privilege -> start-session flow against
 * the initrd account database, verifying success and every failure mode,
 * then restores the pristine slot-0 root identity so the live kernel PCB is
 * untouched.
 *
 * Default seeded accounts (see initrd64.c):
 *   root   / password "root"   -> uid 0  gid 0
 *   openos / password "openos" -> unprivileged uid/gid
 */
#include "../include/login_selftest64.h"
#include "../include/login64.h"
#include "../include/account_db64.h"
#include "../include/proc64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stddef.h>

static void lg_log(const char *s) { early_console64_write(s); }

bool arch_x86_64_login_selftest_run(void)
{
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == NULL) {
        lg_log("[x86_64][login-selftest] FAIL: current PCB is NULL\n");
        return false;
    }

    /* Snapshot pristine credentials + session so we can restore them. */
    uint32_t s_uid  = p->uid,  s_gid  = p->gid;
    uint32_t s_euid = p->euid, s_egid = p->egid;
    uint32_t s_suid = p->suid, s_sgid = p->sgid;
    uint32_t s_pgid = p->pgid, s_sid  = p->sid;

    bool ok = true;
    #define LOGIN_CHECK(cond, msg)                                     \
        do {                                                           \
            if (!(cond)) {                                             \
                lg_log("[x86_64][login-selftest] FAIL: " msg "\n");    \
                ok = false;                                            \
            }                                                          \
        } while (0)

    /* --- Stage 1: authenticate root with the correct password. --- */
    {
        x86_64_passwd_entry_t pw;
        x86_64_login_result_t r =
            arch_x86_64_login_authenticate("root", "root", &pw);
        LOGIN_CHECK(r == X86_64_LOGIN_OK, "stage1 root auth should OK");
        LOGIN_CHECK(pw.uid == 0u, "stage1 root uid!=0");
        LOGIN_CHECK(pw.gid == 0u, "stage1 root gid!=0");
    }

    /* --- Stage 2: authenticate the unprivileged account. --- */
    uint32_t openos_uid = 0u, openos_gid = 0u;
    {
        x86_64_passwd_entry_t pw;
        x86_64_login_result_t r =
            arch_x86_64_login_authenticate("openos", "openos", &pw);
        LOGIN_CHECK(r == X86_64_LOGIN_OK, "stage2 openos auth should OK");
        LOGIN_CHECK(pw.uid != 0u, "stage2 openos uid should be non-root");
        openos_uid = pw.uid;
        openos_gid = pw.gid;
    }

    /* --- Stage 3: wrong password is rejected. --- */
    {
        x86_64_login_result_t r =
            arch_x86_64_login_authenticate("root", "wrongpw", NULL);
        LOGIN_CHECK(r == X86_64_LOGIN_BAD_PASSWORD,
                    "stage3 wrong password should BAD_PASSWORD");
    }

    /* --- Stage 4: unknown account is rejected. --- */
    {
        x86_64_login_result_t r =
            arch_x86_64_login_authenticate("nobody_xyz", "x", NULL);
        LOGIN_CHECK(r == X86_64_LOGIN_NO_USER,
                    "stage4 unknown user should NO_USER");
    }

    /* --- Stage 5: empty/NULL arguments are rejected. --- */
    {
        x86_64_login_result_t r =
            arch_x86_64_login_authenticate(NULL, "x", NULL);
        LOGIN_CHECK(r == X86_64_LOGIN_EINVAL, "stage5 NULL name should EINVAL");
    }

    /*
     * --- Stage 6: start_session() drops the current PCB to the openos
     * account and makes it a session leader. Work on a temporary root PCB
     * state so the drop rules (must be root to setgid/setuid to arbitrary
     * ids) are satisfied.
     */
    if (openos_uid != 0u) {
        p->uid = p->euid = p->suid = 0u;
        p->gid = p->egid = p->sgid = 0u;

        x86_64_passwd_entry_t pw;
        x86_64_login_result_t r =
            arch_x86_64_login_authenticate("openos", "openos", &pw);
        LOGIN_CHECK(r == X86_64_LOGIN_OK, "stage6 re-auth should OK");

        r = arch_x86_64_login_start_session(&pw);
        LOGIN_CHECK(r == X86_64_LOGIN_OK, "stage6 start_session should OK");
        LOGIN_CHECK(arch_x86_64_proc_current_uid() == openos_uid,
                    "stage6 ruid should be openos uid");
        LOGIN_CHECK(arch_x86_64_proc_current_gid() == openos_gid,
                    "stage6 rgid should be openos gid");
        /* setsid makes sid == pid (session leader). */
        LOGIN_CHECK(p->sid == p->pid, "stage6 sid should equal pid");
    }

    /* --- Restore the pristine slot-0 root identity. --- */
    p->uid  = s_uid;  p->gid  = s_gid;
    p->euid = s_euid; p->egid = s_egid;
    p->suid = s_suid; p->sgid = s_sgid;
    p->pgid = s_pgid; p->sid  = s_sid;

    if (ok) {
        lg_log("[x86_64][login-selftest] PASS\n");
    }
    #undef LOGIN_CHECK
    return ok;
}
