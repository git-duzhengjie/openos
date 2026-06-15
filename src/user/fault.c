/* openos - user exception isolation test */

#include "openos.h"

void _start(void)
{
    const char *msg = "About to trigger user page fault...\n";

    openos_write_str(msg);

    volatile unsigned int *bad = (volatile unsigned int *)0x00000000;
    *bad = 0x12345678;

    openos_exit(1);
}
