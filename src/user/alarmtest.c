/* ============================================================
 * openos - alarm signal smoke test
 * ============================================================ */

#include "openos.h"

static void write_err(const char *s)
{
    openos_write_fd(STDERR_FILENO, s, openos_strlen(s));
}

int main(int argc, char **argv)
{
    unsigned int seconds = 1;

    if (argc > 1) {
        int v = openos_atoi(argv[1]);
        if (v > 0)
            seconds = (unsigned int)v;
    }

    openos_write_str("alarmtest: arming alarm for ");
    openos_print_int((int)seconds);
    openos_write_str(" second(s); process should exit with SIGALRM\n");

    if (openos_alarm(seconds) < 0) {
        write_err("alarmtest: alarm failed\n");
        return 1;
    }

    while (1) {
        openos_sleep(100);
        openos_write_str(".");
    }

    return 0;
}
