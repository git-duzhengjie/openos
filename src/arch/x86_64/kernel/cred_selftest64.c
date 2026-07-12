#include "../include/cred_selftest64.h"
#include "../include/proc64.h"
#include "../include/early_console64.h"
#include <stdint.h>
#include <stddef.h>

static void cred_log(const char *s) { early_console64_write(s); }

/*
 * M6.11.1 credential selftest.
 *
 * The running kernel PCB (slot 0) is root, so the API always starts from
 * euid==0. We drive the setuid/setgid/seteuid/setegid privilege machine
 * through a scripted sequence, checking every real/effective/saved id after
 * each transition, then restore the PCB to pristine root so the live kernel
 * identity is untouched.
 */
bool arch_x86_64_cred_selftest_run(void) {
    x86_64_proc_t *p = arch_x86_64_proc_current();
    if (p == NULL) {
        cred_log("[x86_64][cred-selftest] FAIL: current PCB is NULL\n");
        return false;
    }

    /* Snapshot the pristine credentials so we can restore them later. */
    uint32_t s_uid  = p->uid,  s_gid  = p->gid;
    uint32_t s_euid = p->euid, s_egid = p->egid;
    uint32_t s_suid = p->suid, s_sgid = p->sgid;

    bool ok = true;
    #define CRED_CHECK(cond, msg)                                   \
        do {                                                        \
            if (!(cond)) {                                          \
                cred_log("[x86_64][cred-selftest] FAIL: " msg "\n");\
                ok = false;                                         \
            }                                                       \
        } while (0)

    /* --- Stage 1: start as root across the whole set. --- */
    p->uid = p->euid = p->suid = 0u;
    p->gid = p->egid = p->sgid = 0u;
    CRED_CHECK(arch_x86_64_proc_current_euid() == 0u, "stage1 euid!=0");
    CRED_CHECK(arch_x86_64_proc_current_uid()  == 0u, "stage1 uid!=0");

    /* --- Stage 2: root setuid(1000) collapses ruid=euid=suid=1000
     *              (the classic irreversible privilege drop). --- */
    CRED_CHECK(arch_x86_64_proc_setuid(1000u) == 0, "stage2 setuid rc");
    CRED_CHECK(arch_x86_64_proc_current_uid()  == 1000u, "stage2 ruid");
    CRED_CHECK(arch_x86_64_proc_current_euid() == 1000u, "stage2 euid");
    CRED_CHECK(arch_x86_64_proc_current_suid() == 1000u, "stage2 suid");

    /* --- Stage 3: now unprivileged, setuid to an unrelated id is EPERM. --- */
    CRED_CHECK(arch_x86_64_proc_setuid(2000u) == -1, "stage3 setuid should EPERM");
    CRED_CHECK(arch_x86_64_proc_current_uid()  == 1000u, "stage3 ruid unchanged");
    CRED_CHECK(arch_x86_64_proc_current_euid() == 1000u, "stage3 euid unchanged");

    /* --- Stage 4: seteuid to the real uid is allowed (no-op here). --- */
    CRED_CHECK(arch_x86_64_proc_seteuid(1000u) == 0, "stage4 seteuid real");

    /* --- Stage 5: craft a saved-uid so a temporary drop/restore works.
     *              Manufacture euid=1000, suid=0 as a setuid-root program
     *              would have after exec, then verify seteuid can toggle
     *              between real(1000) and saved(0) but not to a stranger. --- */
    p->uid = 1000u; p->euid = 1000u; p->suid = 0u;
    CRED_CHECK(arch_x86_64_proc_seteuid(0u) == 0, "stage5 restore-to-saved");
    CRED_CHECK(arch_x86_64_proc_current_euid() == 0u, "stage5 euid==saved");
    /* euid is now 0 (root) -> seteuid to anything is allowed again. */
    CRED_CHECK(arch_x86_64_proc_seteuid(1000u) == 0, "stage5 drop-again");
    /* back to unprivileged; a stranger uid must fail. */
    CRED_CHECK(arch_x86_64_proc_seteuid(4242u) == -1, "stage5 stranger EPERM");

    /* --- Stage 6: group-id mutators mirror the uid rules. --- */
    p->gid = p->egid = p->sgid = 0u;
    p->uid = p->euid = p->suid = 0u;              /* re-arm root for setgid */
    CRED_CHECK(arch_x86_64_proc_setgid(1000u) == 0, "stage6 setgid rc");
    CRED_CHECK(arch_x86_64_proc_current_gid()  == 1000u, "stage6 rgid");
    CRED_CHECK(arch_x86_64_proc_current_egid() == 1000u, "stage6 egid");
    CRED_CHECK(arch_x86_64_proc_current_sgid() == 1000u, "stage6 sgid");
    /* now unprivileged (egid!=0 but euid==0 still root -> keep euid root check
     * in mind: setgid rule keys on euid, so drop euid to make it unprivileged) */
    p->euid = 1000u;
    CRED_CHECK(arch_x86_64_proc_setegid(2000u) == -1, "stage6 setegid stranger EPERM");
    CRED_CHECK(arch_x86_64_proc_setegid(1000u) == 0,  "stage6 setegid real ok");

    #undef CRED_CHECK

    /* Restore the pristine kernel identity no matter what happened. */
    p->uid  = s_uid;  p->gid  = s_gid;
    p->euid = s_euid; p->egid = s_egid;
    p->suid = s_suid; p->sgid = s_sgid;

    if (ok) {
        cred_log("[x86_64][cred-selftest] PASS: uid/gid credential rules "
                 "(setuid drop, seteuid real/saved toggle, EPERM guard)\n");
    }
    return ok;
}
