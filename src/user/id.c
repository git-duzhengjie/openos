#include "openos.h"

static void write_uint(unsigned int v)
{
    char buf[16];
    int i = 0;
    int j;

    if (v == 0) {
        openos_write_fd(STDOUT_FILENO, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (j = i - 1; j >= 0; j--)
        openos_write_fd(STDOUT_FILENO, &buf[j], 1);
}

static void write_name_or_unknown(const char *name)
{
    if (name && name[0])
        openos_write_fd(STDOUT_FILENO, name, openos_strlen(name));
    else
        openos_write_fd(STDOUT_FILENO, "unknown", 7);
}

void _start(int argc, char **argv)
{
    unsigned int uid;
    unsigned int gid;
    openos_user_t user;
    openos_group_t group;
    int have_user;
    int have_group;

    if (argc > 1 && openos_strcmp(argv[1], "--help") == 0) {
        openos_write_fd(STDOUT_FILENO, "usage: id\n", 10);
        openos_exit(0);
    }

    uid = (unsigned int)openos_getuid();
    gid = (unsigned int)openos_getgid();
    have_user = (openos_getpwuid(uid, &user) == 0);
    have_group = (openos_getgrgid(gid, &group) == 0);

    openos_write_fd(STDOUT_FILENO, "uid=", 4);
    write_uint(uid);
    openos_write_fd(STDOUT_FILENO, "(", 1);
    write_name_or_unknown(have_user ? user.name : 0);
    openos_write_fd(STDOUT_FILENO, ") gid=", 6);
    write_uint(gid);
    openos_write_fd(STDOUT_FILENO, "(", 1);
    write_name_or_unknown(have_group ? group.name : 0);
    openos_write_fd(STDOUT_FILENO, ") groups=", 9);
    write_uint(gid);
    openos_write_fd(STDOUT_FILENO, "(", 1);
    write_name_or_unknown(have_group ? group.name : 0);
    openos_write_fd(STDOUT_FILENO, ")\n", 2);
    openos_exit(0);
}
