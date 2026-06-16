/* ============================================================
 * openos - user/kernel memory isolation regression test
 * ============================================================ */

#include "openos.h"

void _start(void)
{
    openos_write_str("[isotest] attempting user write to kernel low memory...\n");

    /* This low identity-mapped kernel address must be supervisor-only. */
    volatile unsigned int *kernel_addr = (volatile unsigned int *)0x00080000;
    *kernel_addr = 0xC0FFEE00u;

    openos_fail(1, "[isotest] ERROR: user write to kernel memory succeeded\n");
}
