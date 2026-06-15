#include "openos.h"

static void mkdir_usage(void)
{
    openos_write_str("usage: mkdir dir...\n");
}

void _start(int argc, char **argv, char **envp)
{
    int i;
    int failed = 0;

    (void)envp;

    if (argc < 2) {
        mkdir_usage();
        openos_exit(1);
    }

    for (i = 1; i < argc; i++) {
        if (!argv[i] || argv[i][0] == '\0') {
            openos_write_str("mkdir: invalid path\n");
            failed = 1;
            continue;
        }

        if (openos_mkdir(argv[i], 0755) < 0) {
            openos_write_str("mkdir: cannot create directory ");
            openos_write_str(argv[i]);
            openos_write_str("\n");
            failed = 1;
        }
    }

    openos_exit(failed ? 1 : 0);
}
