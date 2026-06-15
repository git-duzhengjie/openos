/*
 * openos - orphan reparent regression helper
 *
 * Spawns a child and exits without waiting. The kernel should reparent
 * the child to init/reaper and later reap it automatically.
 */

#include "openos.h"

void _start(void)
{
    int child = openos_spawn("/bin/exit42", 0);
    if (child < 0)
        openos_exit(100);

    openos_exit(7);
}
