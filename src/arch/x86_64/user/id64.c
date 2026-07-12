/*
 * id64.c — M6.11.3 ring3 `id` utility.
 *
 * Prints the calling process credentials in the classic id(1) layout:
 *
 *   uid=1000 gid=1000 euid=0 egid=0
 *
 * The real ids come from getuid/getgid; the effective ids from
 * geteuid/getegid. When effective == real the euid/egid fields are
 * omitted, matching coreutils behaviour. This is the headless CLI proof
 * that the M6.11.1 credential syscalls are reachable from ring3.
 */
#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/stdio.h"

int openos64_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    long uid  = openos64_getuid();
    long gid  = openos64_getgid();
    long euid = openos64_geteuid();
    long egid = openos64_getegid();

    /* Base real-id fields, always printed. */
    printf("uid=%ld gid=%ld", uid, gid);

    /* Effective ids only when they differ from the real ids. */
    if (euid != uid) {
        printf(" euid=%ld", euid);
    }
    if (egid != gid) {
        printf(" egid=%ld", egid);
    }
    printf("\n");

    return 0;
}
