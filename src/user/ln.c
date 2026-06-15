#include "openos.h"

static void usage(void)
{
    openos_write_str(2, "usage: ln [-s] OLD NEW\n");
}

int main(int argc, char **argv)
{
    int symbolic = 0;
    const char *oldpath;
    const char *newpath;

    if (argc == 4 && openos_str_equal(argv[1], "-s")) {
        symbolic = 1;
        oldpath = argv[2];
        newpath = argv[3];
    } else if (argc == 3) {
        oldpath = argv[1];
        newpath = argv[2];
    } else {
        usage();
        return 1;
    }

    if (symbolic) {
        if (openos_symlink(oldpath, newpath) < 0) {
            openos_write_str(2, "ln: cannot create symbolic link '");
            openos_write_str(2, newpath);
            openos_write_str(2, "' to '");
            openos_write_str(2, oldpath);
            openos_write_str(2, "'\n");
            return 1;
        }
        return 0;
    }

    if (openos_link(oldpath, newpath) < 0) {
        openos_write_str(2, "ln: cannot create hard link '");
        openos_write_str(2, newpath);
        openos_write_str(2, "' to '");
        openos_write_str(2, oldpath);
        openos_write_str(2, "'\n");
        return 1;
    }

    return 0;
}
