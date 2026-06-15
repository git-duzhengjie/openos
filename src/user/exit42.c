/*
 * openos - exit status regression helper
 */

#include "openos.h"

void _start(void)
{
    openos_exit(42);
}
