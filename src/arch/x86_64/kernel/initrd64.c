#include "../include/initrd64.h"

#include <stddef.h>

#include "../include/early_console64.h"

/* H.2: hello64.elf 经由 initrd 通路供 ELF loader 在运行时查找加载，
 * 不再让 kernel64.c 直接 #include embed_hello64.h。此处是
 * embed_hello64.h 的唯一 consumer，保留"编译期嵌入"的当前形态，
 * 但已把 image 来源抽象到 initrd 路径 /bin/hello64 之后，
 * 将来要换成真正的 boot-loaded initrd 也只动这一处。 */
#include "../include/embed_hello64.h"
#include "../include/embed_hello64_v2.h"  /* H.3: execve target image */
#include "../include/embed_hello_fork.h"  /* A2.P5: standalone fork/wait target */
#include "../include/embed_thread_demo.h" /* M5.2d: clone+futex+pthread target */
#include "../include/embed_libc_demo.h"    /* M5.3e: standard C library subset e2e */
#include "../include/embed_fs_demo.h"      /* M5.4a: writable-VFS e2e */
#include "../include/embed_launcher.h"     /* H.3: initial /bin/launcher  */
#include "../include/embed_ifconfig64.h"   /* M1.5.3: /bin/ifconfig net tool */
#include "../include/embed_ping64.h"       /* M1.5.3: /bin/ping net tool */
#include "../include/embed_nslookup64.h"   /* M1.5.3: /bin/nslookup net tool */
#include "../include/embed_wget64.h"       /* M1.7: /bin/wget ring3 TCP tool */
#include "../include/embed_i18n_en.h"      /* i18n: /etc/i18n/en.json 译文数据源 */
#include "../include/embed_i18n_zh.h"      /* i18n: /etc/i18n/zh.json 译文数据源 */

static const uint8_t init_script[] =
    "echo OpenOS x86_64 initrd mounted\n"
    "cat /etc/motd\n"
    "echo shell ready\n";

static const uint8_t motd[] =
    "Welcome to OpenOS x86_64 userspace bootstrap.\n";

static const uint8_t shell_profile[] =
    "PATH=/bin\n"
    "PROMPT=openos64> \n";

/* Step C：x86_64 用户态 read regression 专用文件。
 * 内容刻意与 /etc/motd 不同，便于在串口/VGA 中肉眼区分。 */
static const uint8_t hello_txt[] =
    "hello from x86_64 initrd via syscall read\n";

static const x86_64_initrd_file_t initrd_files[] = {
    { .name = "/init", .data = init_script, .size = sizeof(init_script) - 1u, .mode = 0755u },
    { .name = "/etc/motd", .data = motd, .size = sizeof(motd) - 1u, .mode = 0644u },
    { .name = "/etc/profile", .data = shell_profile, .size = sizeof(shell_profile) - 1u, .mode = 0644u },
    { .name = "/hello.txt", .data = hello_txt, .size = sizeof(hello_txt) - 1u, .mode = 0644u },
    /* H.2: ring3 demo ELF。kernel64.c 通过 initrd_find("/bin/hello64")
     * 拿到 image 后喂给 arch_x86_64_elf64_load_image。 */
    { .name = "/bin/hello64", .data = hello64_elf, .size = (x86_64_size_t)hello64_elf_size, .mode = 0755u },
    /* H.3: execve demo pair. /bin/launcher is what the kernel jumps into
     * first; it execve's /bin/hello64_v2 to prove the trampoline works. */
    { .name = "/bin/launcher",    .data = launcher_elf,    .size = (x86_64_size_t)launcher_elf_size,    .mode = 0755u },
    { .name = "/bin/hello64_v2", .data = hello64_v2_elf, .size = (x86_64_size_t)hello64_v2_elf_size, .mode = 0755u },
    /* A2.P5: fork/wait standalone smoke test image, executed via a second
     * execve inside /bin/hello64_v2. Splitting the historical inline [wait]
     * block out here decouples execve and fork/wait regressions. */
    { .name = "/bin/hello_fork", .data = hello_fork_elf, .size = (x86_64_size_t)hello_fork_elf_size, .mode = 0755u },
    /* M5.2d: /bin/thread_demo exercises clone/futex/pthread from ring3.
     * /bin/hello_fork execve's into it as the tail of the launch chain. */
    { .name = "/bin/thread_demo", .data = thread_demo_elf, .size = (x86_64_size_t)thread_demo_elf_size, .mode = 0755u },
    /* M5.3e: /bin/libc_demo exercises the standard C library subset
     * (memcpy/malloc/printf/qsort/...). /bin/thread_demo execve's into it. */
    { .name = "/bin/libc_demo", .data = libc_demo_elf, .size = (x86_64_size_t)libc_demo_elf_size, .mode = 0755u },
    /* M5.4a: /bin/fs_demo exercises the writable VFS (mkdir/open/write/read/
     * lseek/stat/unlink/rmdir). /bin/libc_demo execve's into it. */
    { .name = "/bin/fs_demo", .data = fs_demo_elf, .size = (x86_64_size_t)fs_demo_elf_size, .mode = 0755u },
    /* M1.5.3: userland network tools backed by the live virtio-net TCP/IP
     * stack (SYS_NETINFO/PING/DNSLOOKUP). */
    { .name = "/bin/ifconfig", .data = ifconfig64_elf, .size = (x86_64_size_t)ifconfig64_elf_size, .mode = 0755u },
    { .name = "/bin/ping",     .data = ping64_elf,     .size = (x86_64_size_t)ping64_elf_size,     .mode = 0755u },
    { .name = "/bin/nslookup", .data = nslookup64_elf, .size = (x86_64_size_t)nslookup64_elf_size, .mode = 0755u },
    { .name = "/bin/wget", .data = wget64_elf, .size = (x86_64_size_t)wget64_elf_size, .mode = 0755u },
    /* i18n: yi JSON wei wei-yi shu-ju-yuan (res/i18n/en.json, zh.json).
     * yun-xing-shi i18n.c jing VFS du /etc/i18n/{en,zh}.json jie-xi tian-biao. */
    { .name = "/etc/i18n/en.json", .data = i18n_en_json, .size = (x86_64_size_t)i18n_en_json_size, .mode = 0644u },
    { .name = "/etc/i18n/zh.json", .data = i18n_zh_json, .size = (x86_64_size_t)i18n_zh_json_size, .mode = 0644u },
};

static const x86_64_initrd_image_t builtin_initrd = {
    .magic = OPENOS_X86_64_INITRD_MAGIC,
    .file_count = (uint32_t)(sizeof(initrd_files) / sizeof(initrd_files[0])),
    .files = initrd_files,
};

static uint8_t initrd_ready;
static uint8_t initrd_from_bootinfo;
static uint64_t initrd_bootinfo_base;
static uint64_t initrd_bootinfo_size;
static uint64_t initrd_lookup_count;

static int initrd_streq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

void arch_x86_64_initrd_init(const openos_bootinfo_t *bootinfo) {
    initrd_ready = 1u;
    initrd_from_bootinfo = 0u;
    initrd_bootinfo_base = 0u;
    initrd_bootinfo_size = 0u;
    initrd_lookup_count = 0;

    if (openos_bootinfo_is_valid(bootinfo) &&
        (bootinfo->flags & OPENOS_BOOTINFO_FLAG_INITRD_VALID)) {
        initrd_from_bootinfo = 1u;
        initrd_bootinfo_base = bootinfo->initrd.base;
        initrd_bootinfo_size = bootinfo->initrd.size;
    }
}

const x86_64_initrd_image_t *arch_x86_64_initrd_get_image(void) {
    return initrd_ready ? &builtin_initrd : NULL;
}

const x86_64_initrd_file_t *arch_x86_64_initrd_find(const char *path) {
    uint32_t i;
    ++initrd_lookup_count;
    if (!initrd_ready || path == NULL) {
        return NULL;
    }
    for (i = 0; i < builtin_initrd.file_count; ++i) {
        if (initrd_streq(path, builtin_initrd.files[i].name)) {
            return &builtin_initrd.files[i];
        }
    }
    return NULL;
}

void arch_x86_64_initrd_print_status(void) {
    early_console64_write("[x86_64][initrd] ready=");
    early_console64_write_hex64(initrd_ready);
    early_console64_write(" files=");
    early_console64_write_hex64(builtin_initrd.file_count);
    early_console64_write(" lookups=");
    early_console64_write_hex64(initrd_lookup_count);
    early_console64_write(" from_bootinfo=");
    early_console64_write_hex64(initrd_from_bootinfo);
    early_console64_write(" bootinfo_base=");
    early_console64_write_hex64(initrd_bootinfo_base);
    early_console64_write(" bootinfo_size=");
    early_console64_write_hex64(initrd_bootinfo_size);
    early_console64_write(" magic=");
    early_console64_write_hex64(builtin_initrd.magic);
    early_console64_write("\n");
}
