/* ============================================================
 * openos - echo user command
 * ============================================================ */

#include "openos.h"

void _start(int argc, char **argv, char **envp)
{
    int i;

    (void)envp;

    for (i = 1; i < argc; i++) {
        if (i > 1)
            openos_write_str(" ");
        openos_write_str(argv[i]);
    }
    openos_write_str("\n");

    openos_exit(0);
}
