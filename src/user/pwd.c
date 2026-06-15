/* openos - pwd user command */

#include "openos.h"

void _start(int argc, char **argv, char **envp)
{
    char cwd[OPENOS_PATH_MAX];

    (void)argc;
    (void)argv;
    (void)envp;

    if (openos_getcwd(cwd, sizeof(cwd)) < 0)
        openos_fail(1, "pwd: getcwd failed\n");

    openos_write_str(cwd);
    openos_write_str("\n");
    openos_exit(0);
}
