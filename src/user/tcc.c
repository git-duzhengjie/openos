/* OPENOS built-in TinyCC command.
 * TinyCC is used as the full C compiler core; this file supplies the OPENOS
 * userspace configuration, compatibility layer and default sysroot arguments.
 */
#include "openos_tcc_compat.h"

#define main openos_tinycc_main
#include "../../ports/tinycc/tcc.c"
#undef main

static int openos_tcc_has_option(int argc, char **argv, const char *short_opt, const char *long_opt)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (short_opt && strcmp(argv[i], short_opt) == 0)
            return 1;
        if (long_opt && strcmp(argv[i], long_opt) == 0)
            return 1;
    }
    return 0;
}

static int openos_tcc_is_compile_only(int argc, char **argv)
{
    return openos_tcc_has_option(argc, argv, "-c", NULL) ||
           openos_tcc_has_option(argc, argv, "-E", NULL) ||
           openos_tcc_has_option(argc, argv, "-S", NULL) ||
           openos_tcc_has_option(argc, argv, "-r", NULL) ||
           openos_tcc_has_option(argc, argv, "-ar", NULL) ||
           openos_tcc_has_option(argc, argv, NULL, "-print-search-dirs") ||
           openos_tcc_has_option(argc, argv, "-h", NULL) ||
           openos_tcc_has_option(argc, argv, "--help", NULL);
}

static const char *openos_tcc_output_path(int argc, char **argv)
{
    int i;
    for (i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0)
            return argv[i + 1];
    }
    return "a.out";
}

int main(int argc, char **argv)
{
    char *new_argv[64];
    int n = 0;
    int i;
    int link_exe;
    int rc;
    const char *output_path;

    if (argc <= 1)
        return openos_tinycc_main(argc, argv);

    link_exe = !openos_tcc_is_compile_only(argc, argv);
    output_path = link_exe ? openos_tcc_output_path(argc, argv) : NULL;

    new_argv[n++] = argv[0];
    new_argv[n++] = "-B/usr/lib/tcc";
    new_argv[n++] = "-I/usr/include";
    new_argv[n++] = "-I/usr/include/tcc";
    new_argv[n++] = "-D__OPENOS__=1";
    new_argv[n++] = "-D__i386__=1";
    new_argv[n++] = "-fno-pic";
    new_argv[n++] = "-fno-pie";

    if (link_exe) {
        new_argv[n++] = "-nostdlib";
        new_argv[n++] = "-Wl,-e,_start";
        new_argv[n++] = "-Wl,-Ttext=0x40000000";
        new_argv[n++] = "-Wl,-section-alignment=0x1000";
        new_argv[n++] = "/usr/lib/tcc/crt0.o";
    }

    for (i = 1; i < argc && n < (int)(sizeof(new_argv) / sizeof(new_argv[0])) - 1; ++i)
        new_argv[n++] = argv[i];

    new_argv[n] = NULL;
    rc = openos_tinycc_main(n, new_argv);
    if (rc == 0 && link_exe && output_path) {
        openos_chmod(output_path, FS_FILE | S_IRUSR | S_IWUSR | S_IXUSR |
                                  S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        openos_write_str("tcc: output written to ");
        openos_write_str(output_path);
        openos_write_str("\n");
        openos_write_str("tcc: run it with: ");
        openos_write_str(output_path);
        openos_write_str("\n");
    }
    return rc;
}
