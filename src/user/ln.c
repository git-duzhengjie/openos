#include "openos.h"

static void write_err(const char *s)
{
    openos_write_fd(2, s, openos_strlen(s));
}

static void usage(void)
{
    write_err("usage: ln [-s] OLD NEW\n");
}

int main(int argc, char **argv)
{
    int symbolic = 0;
    const char *oldpath;
    const char *newpath;

    if (argc == 4 && openos_strcmp(argv[1], "-s") == 0) {
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
            write_err("ln: cannot create symbolic link '");
            write_err(newpath);
            write_err("' to '");
            write_err(oldpath);
            write_err("'\n");
            return 1;
        }
        return 0;
    }

    if (openos_link(oldpath, newpath) < 0) {
        write_err("ln: cannot create hard link '");
        write_err(newpath);
        write_err("' to '");
        write_err(oldpath);
        write_err("'\n");
        return 1;
    }

    return 0;
}
