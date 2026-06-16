/* ============================
 * openos userland: nicetest
 * Validate priority/nice syscalls.
 * ============================ */
#include "openos.h"

static void print_value(const char *name, int value)
{
    openos_write(1, name, openos_strlen(name));
    openos_print_int(value);
    openos_write(1, "\n", 1);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int initial = openos_getpriority(0);
    if (initial < -20 || initial > 19) {
        openos_write(1, "nicetest: getpriority failed\n", 30);
        return 1;
    }
    print_value("initial nice=", initial);

    if (openos_setpriority(0, 10) < 0) {
        openos_write(1, "nicetest: setpriority failed\n", 30);
        return 1;
    }

    int lowered = openos_getpriority(0);
    print_value("after set nice=", lowered);
    if (lowered < 9 || lowered > 10) {
        openos_write(1, "nicetest: unexpected setpriority result\n", 40);
        return 1;
    }

    int adjusted = openos_nice(-5);
    print_value("after nice(-5)=", adjusted);
    if (adjusted < 4 || adjusted > 6) {
        openos_write(1, "nicetest: unexpected nice result\n", 33);
        return 1;
    }

    if (openos_setpriority(0, initial) < 0) {
        openos_write(1, "nicetest: restore failed\n", 25);
        return 1;
    }

    openos_write(1, "nicetest: ok\n", 13);
    return 0;
}
