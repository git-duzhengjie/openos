/* openos - minimal ring3 ELF smoke test */

#include "openos.h"

void _start(void)
{
    const char *msg = "Hello from ring3 ELF via int 0x80!\n";

    openos_write_str(msg);
    (void)openos_getpid();

    openos_exit(0);
}
