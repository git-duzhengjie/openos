/* ============================================================
 * openos - minimal C runtime entry
 * ============================================================ */

#include "openos.h"

int main(int argc, char **argv, char **envp);

void _start(int argc, char **argv, char **envp)
{
    int code = main(argc, argv, envp);
    openos_exit(code);
}
