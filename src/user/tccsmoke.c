/* ============================================================
 * openos - TinyCC in-system smoke test
 * ============================================================ */

#include "openos.h"

static int failures = 0;

static int cstrlen(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void say(const char *s)
{
    openos_write(1, s, cstrlen(s));
}

static void say_num(int v)
{
    char buf[32];
    openos_itoa(v, buf, 10);
    say(buf);
}

static int write_file(const char *path, const char *text)
{
    int fd = openos_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int len = cstrlen(text);
    int n;
    if (fd < 0) {
        say("[tccsmoke] FAIL open write ");
        say(path);
        say("\n");
        failures++;
        return -1;
    }
    n = openos_write_fd(fd, text, len);
    openos_close(fd);
    if (n != len) {
        say("[tccsmoke] FAIL short write ");
        say(path);
        say("\n");
        failures++;
        return -1;
    }
    return 0;
}

static int spawn_wait(char *const argv[])
{
    int status = -1;
    int pid = openos_spawn(argv[0], argv);
    int waited;
    if (pid < 0) {
        say("[tccsmoke] FAIL spawn ");
        say(argv[0]);
        say("\n");
        return -1;
    }
    waited = openos_waitpid(pid, &status, 0);
    if (waited != pid) {
        say("[tccsmoke] FAIL waitpid ");
        say(argv[0]);
        say("\n");
        return -1;
    }
    if (!WIFEXITED(status)) {
        say("[tccsmoke] FAIL not exited ");
        say(argv[0]);
        say("\n");
        return -1;
    }
    return WEXITSTATUS(status);
}

static int compile_one(const char *name, const char *src, const char *out)
{
    char *argv[6];
    int rc;
    argv[0] = "/bin/tcc";
    argv[1] = (char *)src;
    argv[2] = "-o";
    argv[3] = (char *)out;
    argv[4] = 0;
    say("[tccsmoke] compile ");
    say(name);
    say("...\n");
    rc = spawn_wait(argv);
    if (rc != 0) {
        say("[tccsmoke] FAIL compile ");
        say(name);
        say(" rc=");
        say_num(rc);
        say("\n");
        failures++;
        return -1;
    }
    return 0;
}

static int run_expect(const char *name, const char *exe, int expected)
{
    char *argv[2];
    int rc;
    argv[0] = (char *)exe;
    argv[1] = 0;
    say("[tccsmoke] run ");
    say(name);
    say("...\n");
    rc = spawn_wait(argv);
    if (rc != expected) {
        say("[tccsmoke] FAIL run ");
        say(name);
        say(" expected=");
        say_num(expected);
        say(" got=");
        say_num(rc);
        say("\n");
        failures++;
        return -1;
    }
    return 0;
}

static void test_basic(void)
{
    const char *src =
        "#include <openos.h>\n"
        "int add(int a,int b){return a+b;}\n"
        "int main(void){ puts(\"basic-ok\\n\"); return add(20,22); }\n";
    write_file("/home/tcc_basic.c", src);
    if (compile_one("basic", "/home/tcc_basic.c", "/home/tcc_basic") == 0)
        run_expect("basic", "/home/tcc_basic", 42);
}

static void test_preprocessor_types(void)
{
    const char *src =
        "#include <openos.h>\n"
        "#define BASE 7\n"
        "typedef struct Pair { int a; int b; } Pair;\n"
        "enum { EXTRA = 5 };\n"
        "static int sum(int *p, int n){ int s=0; for(int i=0;i<n;i++) s += p[i]; return s; }\n"
        "int main(void){ int arr[3]={BASE,8,9}; Pair p; p.a=sum(arr,3); p.b=EXTRA; return p.a + p.b; }\n";
    write_file("/home/tcc_types.c", src);
    if (compile_one("preprocessor/types", "/home/tcc_types.c", "/home/tcc_types") == 0)
        run_expect("preprocessor/types", "/home/tcc_types", 29);
}

static void test_file_api(void)
{
    const char *src =
        "#include <openos.h>\n"
        "int main(void){ int fd=open(\"/home/tcc_file_out.txt\", O_WRONLY|O_CREAT|O_TRUNC, 0644);"
        " if(fd<0) return 31; if(write(fd, \"abc\", 3)!=3) return 32; close(fd); return 0; }\n";
    write_file("/home/tcc_file.c", src);
    if (compile_one("file-api", "/home/tcc_file.c", "/home/tcc_file") == 0)
        run_expect("file-api", "/home/tcc_file", 0);
}

static void test_multi_file(void)
{
    char *argv[8];
    int rc;
    write_file("/home/tcc_lib.c", "int lib_answer(void){ return 33; }\n");
    write_file("/home/tcc_main.c", "#include <openos.h>\nint lib_answer(void); int main(void){ return lib_answer()+9; }\n");
    argv[0] = "/bin/tcc";
    argv[1] = "/home/tcc_main.c";
    argv[2] = "/home/tcc_lib.c";
    argv[3] = "-o";
    argv[4] = "/home/tcc_multi";
    argv[5] = 0;
    say("[tccsmoke] compile multi-file...\n");
    rc = spawn_wait(argv);
    if (rc != 0) {
        say("[tccsmoke] FAIL compile multi-file rc=");
        say_num(rc);
        say("\n");
        failures++;
        return;
    }
    run_expect("multi-file", "/home/tcc_multi", 42);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    say("[tccsmoke] START\n");
    test_basic();
    test_preprocessor_types();
    test_file_api();
    test_multi_file();
    if (failures == 0) {
        say("[tccsmoke] PASS\n");
        return 0;
    }
    say("[tccsmoke] FAILURES=");
    say_num(failures);
    say("\n");
    return 1;
}
