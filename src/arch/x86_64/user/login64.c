/*
 * login64.c — M6.11.4 ring3 `login` utility.
 *
 * Authenticates a user account via SYS_LOGIN (486) and, on success,
 * establishes a new session (setsid + drops to the account's gid/uid).
 * Usage: login <username> <password>
 *
 * This is the headless CLI proof that M6.11.4 login/session syscall
 * integration works end-to-end from ring3.
 */
#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/stdio.h"

int openos64_main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc != 3) {
        printf("Usage: login <username> <password>\n");
        return 1;
    }

    const char *name = argv[1];
    const char *pass = argv[2];

    printf("[login] authenticating '%s'...\n", name);

    openos64_passwd_entry_t pw;
    long rc = openos64_login(name, pass, &pw);

    if (rc < 0) {
        printf("[login] FAILED: code=%ld\n", rc);
        return 1;
    }

    printf("[login] OK\n");
    printf("  name : %s\n", pw.name);
    printf("  uid  : %u\n", pw.uid);
    printf("  gid  : %u\n", pw.gid);
    printf("  home : %s\n", pw.home);
    printf("  shell: %s\n", pw.shell);

    long uid  = openos64_getuid();
    long gid  = openos64_getgid();
    long euid = openos64_geteuid();
    long egid = openos64_getegid();
    printf("  session: uid=%ld gid=%ld euid=%ld egid=%ld\n", uid, gid, euid, egid);

    return 0;
}
