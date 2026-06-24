/* OPENOS TinyCC generated-program runtime support.
 * This file is compiled by the host build into /usr/lib/tcc/crt0.o and is also
 * installed as source documentation in the in-memory sysroot.  It intentionally
 * includes openos.h directly so programs built inside OPENOS can use the same
 * syscall ABI as built-in user commands.
 */

#include "openos.h"

int main(int argc, char **argv, char **envp);

void _start(int argc, char **argv, char **envp)
{
    int code = main(argc, argv, envp);
    openos_exit(code);
}
