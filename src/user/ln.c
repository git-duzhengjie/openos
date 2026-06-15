#include "openos.h"

static void usage(void)
{
    openos_write_str(2, "usage: ln OLD NEW\n");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage();
        return 1;
    }

    if (openos_link(argv[1], argv[2]) < 0) {
        openos_write_str(2, "ln: cannot create hard link '");
        openos_write_str(2, argv[2]);
        openos_write_str(2, "' to '");
        openos_write_str(2, argv[1]);
        openos_write_str(2, "'\n");
        return 1;
    }

    return 0;
}
