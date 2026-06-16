#include "openos.h"

void _start(int argc, char **argv)
{
    unsigned int gid;
    openos_group_t group;

    if (argc > 1 && openos_strcmp(argv[1], "--help") == 0) {
        openos_write_fd(STDOUT_FILENO, "usage: groups\n", 14);
        openos_exit(0);
    }

    gid = (unsigned int)openos_getgid();
    if (openos_getgrgid(gid, &group) == 0 && group.name[0])
        openos_write_fd(STDOUT_FILENO, group.name, openos_strlen(group.name));
    else
        openos_write_fd(STDOUT_FILENO, "unknown", 7);
    openos_write_fd(STDOUT_FILENO, "\n", 1);
    openos_exit(0);
}
