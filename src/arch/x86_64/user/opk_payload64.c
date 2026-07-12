/*
 * M5.4e opk_payload64.c — a *real* ELF payload shipped INSIDE a .opk package.
 *
 * Unlike opk_demo64.c (which builds/installs a package), this program is the
 * thing that gets *installed and then executed*. It is NOT embedded into the
 * initrd; its only route into the running system is:
 *
 *     host packs opk_payload.elf  ->  .opk image bytes
 *     -> embedded into the e2e selftest as a byte array
 *     -> SYS_OPK_INSTALL unpacks it to /pkg/<pkg>/app in the writable ramfs
 *     -> parent fork()s and execve("/pkg/<pkg>/app")
 *     -> this main runs and exits with a well-known code
 *
 * The parent waitpid()s and asserts the exit status, proving the full
 * pack -> install -> execve -> run loop works with a genuine ELF binary
 * that never touched the initrd.
 *
 * Exit code 42 is the "I ran from inside a freshly installed package" signal.
 */

#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/stdio.h"

/* Well-known marker the e2e selftest checks for. */
#define OPK_PAYLOAD_EXIT_CODE 42

int openos64_main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    /* A visible breadcrumb on the serial console when run headless. */
    printf("[opk_payload] hello from inside an installed .opk package!\n");
    printf("[opk_payload] exiting with code %d\n", OPK_PAYLOAD_EXIT_CODE);
    openos64_exit(OPK_PAYLOAD_EXIT_CODE);
    return OPK_PAYLOAD_EXIT_CODE; /* not reached */
}
