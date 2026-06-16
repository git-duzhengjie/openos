#include "openos.h"

typedef struct cap_name {
    unsigned int bit;
    const char *name;
} cap_name_t;

static const cap_name_t g_caps[] = {
    {OPENOS_CAP_SETUID, "setuid"},
    {OPENOS_CAP_SETGID, "setgid"},
    {OPENOS_CAP_NET_ADMIN, "net_admin"},
    {OPENOS_CAP_SYS_ADMIN, "sys_admin"},
    {OPENOS_CAP_KILL, "kill"},
};

static void write_hex(unsigned int value)
{
    static const char hex[] = "0123456789abcdef";
    char buf[10];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++)
        buf[2 + i] = hex[(value >> (28 - i * 4)) & 0xf];
    openos_write_fd(STDOUT_FILENO, buf, 10);
}

void _start(int argc, char **argv)
{
    unsigned int caps;
    int printed = 0;
    unsigned int i;

    if (argc > 1 && openos_strcmp(argv[1], "--help") == 0) {
        openos_write_fd(STDOUT_FILENO, "usage: cap\n", 11);
        openos_exit(0);
    }

    caps = openos_capget();
    openos_write_fd(STDOUT_FILENO, "capabilities=", 13);
    write_hex(caps);
    openos_write_fd(STDOUT_FILENO, " [", 2);
    for (i = 0; i < sizeof(g_caps) / sizeof(g_caps[0]); i++) {
        if ((caps & g_caps[i].bit) == 0)
            continue;
        if (printed)
            openos_write_fd(STDOUT_FILENO, ",", 1);
        openos_write_fd(STDOUT_FILENO, g_caps[i].name, openos_strlen(g_caps[i].name));
        printed = 1;
    }
    if (!printed)
        openos_write_fd(STDOUT_FILENO, "none", 4);
    openos_write_fd(STDOUT_FILENO, "]\n", 2);
    openos_exit(0);
}
