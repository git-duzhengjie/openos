/* ============================================================
 * openos - libc subset regression test
 * ============================================================ */

#include "openos.h"

static void expect_int(int cond, int code, const char *msg)
{
    if (!cond)
        openos_fail(code, msg);
}

void _start(int argc, char **argv)
{
    char buf[32];
    char dst[32];
    char move1[16];
    char move2[16];
    int printed;

    (void)argc;
    (void)argv;

    openos_puts("[libctest] checking libc subset...");

    openos_memset(buf, 'A', 5);
    buf[5] = 0;
    expect_int(openos_strcmp(buf, "AAAAA") == 0, 1, "[libctest] memset failed");

    openos_memcpy(dst, "hello", 6);
    expect_int(openos_strcmp(dst, "hello") == 0, 2, "[libctest] memcpy failed");
    expect_int(openos_memcmp(dst, "hello", 5) == 0, 3, "[libctest] memcmp failed");

    openos_memcpy(move1, "abcdef", 7);
    openos_memmove(move1 + 2, move1, 4);
    expect_int(openos_strcmp(move1, "ababcd") == 0, 4, "[libctest] memmove overlap right failed");

    openos_memcpy(move2, "abcdef", 7);
    openos_memmove(move2, move2 + 2, 4);
    move2[4] = 0;
    expect_int(openos_strcmp(move2, "cdef") == 0, 5, "[libctest] memmove overlap left failed");

    expect_int(openos_strlen("abc") == 3, 6, "[libctest] strlen failed");
    expect_int(openos_strncmp("abcdef", "abcxyz", 3) == 0, 7, "[libctest] strncmp prefix failed");
    expect_int(openos_strncmp("abc", "abd", 3) < 0, 8, "[libctest] strncmp diff failed");
    expect_int(openos_strchr("abc", 'b') != 0, 9, "[libctest] strchr failed");
    expect_int(openos_strrchr("abca", 'a') == "abca" + 3, 10, "[libctest] strrchr failed");
    expect_int(openos_strstr("hello openos", "open") != 0, 11, "[libctest] strstr failed");

    expect_int(openos_isdigit('7') && !openos_isdigit('x'), 12, "[libctest] isdigit failed");
    expect_int(openos_isspace(' ') && openos_isspace('\n') && !openos_isspace('A'), 13, "[libctest] isspace failed");
    expect_int(openos_atoi(" -123xyz") == -123, 14, "[libctest] atoi failed");

    expect_int(openos_itoa(-42, buf, 10) == buf, 15, "[libctest] itoa return failed");
    expect_int(openos_strcmp(buf, "-42") == 0, 16, "[libctest] itoa decimal failed");
    openos_itoa(255, buf, 16);
    expect_int(openos_strcmp(buf, "ff") == 0, 17, "[libctest] itoa hex failed");

    expect_int(openos_putchar('O') == 1, 18, "[libctest] putchar failed");
    openos_putchar('\n');
    printed = openos_printf("[libctest] printf %s %c %d %x %%\n", "ok", 'A', -7, 255);
    expect_int(printed > 0, 19, "[libctest] printf failed");

    openos_puts("[libctest] libc subset ok");
    openos_exit(0);
}
