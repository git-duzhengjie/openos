#include "openos.h"

#define CAP_PASS 0
#define CAP_FAIL 1
#define CAP_SKIP 2

#define MMAP_TEST_SIZE 4096
#define SHM_TEST_WORDS 16

static volatile int g_thread_done = 0;
static volatile int g_thread_value = 0;

static void print_result(const char *name, int status)
{
    if (status == CAP_PASS) {
        openos_printf("[PASS] %s\n", name);
    } else if (status == CAP_SKIP) {
        openos_printf("[SKIP] %s\n", name);
    } else {
        openos_printf("[FAIL] %s\n", name);
    }
}

static void cap_thread_entry(void *arg)
{
    volatile int *value = (volatile int *)arg;
    int tid = openos_gettid();

    if (value) {
        *value = tid > 0 ? tid : 1;
    }
    g_thread_value = 0x4348524f;
    g_thread_done = 1;
    openos_thread_exit(0);
}

static int test_uptime(void)
{
    unsigned int before = openos_uptime_ms();
    openos_sleep(2);
    return openos_uptime_ms() >= before ? CAP_PASS : CAP_FAIL;
}

static int test_mmap(void)
{
    unsigned char *mem;
    int i;

    mem = (unsigned char *)openos_mmap(0, MMAP_TEST_SIZE, 0);
    if (mem == (unsigned char *)-1 || !mem) {
        return CAP_FAIL;
    }

    for (i = 0; i < MMAP_TEST_SIZE; ++i) {
        mem[i] = (unsigned char)(i & 0xff);
    }
    for (i = 0; i < MMAP_TEST_SIZE; ++i) {
        if (mem[i] != (unsigned char)(i & 0xff)) {
            openos_munmap(mem, MMAP_TEST_SIZE);
            return CAP_FAIL;
        }
    }

    return openos_munmap(mem, MMAP_TEST_SIZE) == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_mmap_prot_flags(void)
{
    unsigned char *mem;
    unsigned char value;

    mem = (unsigned char *)openos_mmap_ex(0, 4096,
                                          OPENOS_PROT_READ,
                                          OPENOS_MAP_ANON | OPENOS_MAP_PRIVATE);
    if (mem == (unsigned char *)-1 || !mem) {
        return CAP_FAIL;
    }

    value = mem[0];
    if (value != 0) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }

    if (openos_mprotect(mem, 4096, OPENOS_PROT_READ | OPENOS_PROT_WRITE) != 0) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }

    mem[0] = 0x33;
    if (mem[0] != 0x33) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }

    return openos_munmap(mem, 4096) == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_mmap_fixed(void)
{
    unsigned char *base;
    unsigned char *fixed;
    unsigned char *overlap;

    base = (unsigned char *)openos_mmap(0, 4096, 0);
    if (base == (unsigned char *)-1 || !base) {
        return CAP_FAIL;
    }
    if (openos_munmap(base, 4096) != 0) {
        return CAP_FAIL;
    }

    fixed = (unsigned char *)openos_mmap_ex(base, 4096,
                                           OPENOS_PROT_READ | OPENOS_PROT_WRITE,
                                           OPENOS_MAP_ANON | OPENOS_MAP_PRIVATE | OPENOS_MAP_FIXED);
    if (fixed != base) {
        if (fixed != (unsigned char *)-1)
            openos_munmap(fixed, 4096);
        return CAP_FAIL;
    }

    fixed[0] = 0x44;
    if (fixed[0] != 0x44) {
        openos_munmap(fixed, 4096);
        return CAP_FAIL;
    }

    overlap = (unsigned char *)openos_mmap_ex(base, 4096,
                                             OPENOS_PROT_READ | OPENOS_PROT_WRITE,
                                             OPENOS_MAP_ANON | OPENOS_MAP_PRIVATE | OPENOS_MAP_FIXED);
    if (overlap != (unsigned char *)-1) {
        openos_munmap(overlap, 4096);
        openos_munmap(fixed, 4096);
        return CAP_FAIL;
    }

    return openos_munmap(fixed, 4096) == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_mprotect(void)
{
    unsigned char *mem;

    mem = (unsigned char *)openos_mmap(0, 4096, 0);
    if (mem == (unsigned char *)-1 || !mem) {
        return CAP_FAIL;
    }

    mem[0] = 0x5a;
    if (openos_mprotect(mem, 4096, OPENOS_PROT_READ) != 0) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }
    if (mem[0] != 0x5a) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }
    if (openos_mprotect(mem, 4096, OPENOS_PROT_READ | OPENOS_PROT_WRITE) != 0) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }

    mem[0] = 0xa5;
    if (mem[0] != 0xa5) {
        openos_munmap(mem, 4096);
        return CAP_FAIL;
    }

    return openos_munmap(mem, 4096) == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_file_mmap(void)
{
    const char *path = "/tmp/chromiumcaptest_mmap.txt";
    const char *payload = "OpenOS Chromium file mmap capability";
    int len = openos_strlen(payload);
    int fd = openos_open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    char *mapped;
    int i;

    if (fd < 0)
        return CAP_FAIL;
    if (openos_write_fd(fd, payload, len) != len) {
        openos_close(fd);
        return CAP_FAIL;
    }

    mapped = (char *)openos_mmap_file(fd, 4096, OPENOS_PROT_READ | OPENOS_PROT_WRITE,
                                      OPENOS_MAP_PRIVATE | OPENOS_MAP_FILE);
    if ((int)mapped == -1 || !mapped) {
        openos_close(fd);
        return CAP_FAIL;
    }

    for (i = 0; i < len; ++i) {
        if (mapped[i] != payload[i]) {
            openos_munmap(mapped, 4096);
            openos_close(fd);
            return CAP_FAIL;
        }
    }
    if (mapped[len] != 0) {
        openos_munmap(mapped, 4096);
        openos_close(fd);
        return CAP_FAIL;
    }

    mapped[0] = 'o';
    if (mapped[0] != 'o') {
        openos_munmap(mapped, 4096);
        openos_close(fd);
        return CAP_FAIL;
    }

    if (openos_munmap(mapped, 4096) != 0) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);
    return CAP_PASS;
}

static int test_sbrk(void)
{
    unsigned char *old_break;
    unsigned char *block;
    int i;

    old_break = (unsigned char *)openos_sbrk(0);
    block = (unsigned char *)openos_sbrk(256);
    if (old_break == (unsigned char *)-1 || block == (unsigned char *)-1) {
        return CAP_FAIL;
    }

    for (i = 0; i < 256; ++i) {
        block[i] = (unsigned char)(0xa0 + (i & 0x0f));
    }
    for (i = 0; i < 256; ++i) {
        if (block[i] != (unsigned char)(0xa0 + (i & 0x0f))) {
            return CAP_FAIL;
        }
    }

    return CAP_PASS;
}

static int test_thread(void)
{
    openos_thread_t thread;
    volatile int child_tid = 0;
    unsigned int start;

    g_thread_done = 0;
    g_thread_value = 0;

    if (openos_thread_create(&thread, cap_thread_entry, (void *)&child_tid) != 0) {
        return CAP_FAIL;
    }

    start = openos_uptime_ms();
    while (!g_thread_done && openos_uptime_ms() - start < 1000u) {
        openos_sleep(1);
    }

    if (!g_thread_done || child_tid <= 0 || g_thread_value != 0x4348524f) {
        return CAP_FAIL;
    }

    return CAP_PASS;
}

static int test_shm(void)
{
    openos_shm_t shm;
    volatile unsigned int *a;
    volatile unsigned int *b;
    int i;

    if (openos_shm_create(&shm) != 0) {
        return CAP_FAIL;
    }

    a = (volatile unsigned int *)openos_shm_map(&shm);
    b = (volatile unsigned int *)openos_shm_map(&shm);
    if (a == (volatile unsigned int *)-1 || b == (volatile unsigned int *)-1 || !a || !b) {
        openos_shm_destroy(&shm);
        return CAP_FAIL;
    }

    for (i = 0; i < SHM_TEST_WORDS; ++i) {
        a[i] = 0x1000u + (unsigned int)i;
    }
    for (i = 0; i < SHM_TEST_WORDS; ++i) {
        if (b[i] != 0x1000u + (unsigned int)i) {
            openos_shm_destroy(&shm);
            return CAP_FAIL;
        }
    }

    b[3] = 0xfeedbeefu;
    if (a[3] != 0xfeedbeefu) {
        openos_shm_destroy(&shm);
        return CAP_FAIL;
    }

    return openos_shm_destroy(&shm) == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_eventfd(void)
{
    openos_eventfd_t efd;
    unsigned int value = 0;

    if (openos_eventfd_create(&efd, 1) != 0) {
        return CAP_FAIL;
    }
    if (openos_eventfd_write(&efd, 41) != 0) {
        openos_eventfd_destroy(&efd);
        return CAP_FAIL;
    }
    if (openos_eventfd_read(&efd, &value) != 0) {
        openos_eventfd_destroy(&efd);
        return CAP_FAIL;
    }
    if (value != 42) {
        openos_eventfd_destroy(&efd);
        return CAP_FAIL;
    }

    return openos_eventfd_destroy(&efd) == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_socketpair_poll(void)
{
    int sv[2];
    char ch = 'C';
    char out = 0;
    openos_pollfd_t pfd;
    int ready;

    if (openos_socketpair(OPENOS_AF_UNSPEC, OPENOS_SOCK_STREAM, 0, sv) != 0) {
        return CAP_FAIL;
    }

    if (openos_send(sv[0], &ch, 1, 0) != 1) {
        openos_close(sv[0]);
        openos_close(sv[1]);
        return CAP_FAIL;
    }

    pfd.fd = sv[1];
    pfd.events = OPENOS_POLLIN;
    pfd.revents = 0;
    ready = openos_poll(&pfd, 1, 100);
    if (ready <= 0 || !(pfd.revents & OPENOS_POLLIN)) {
        openos_close(sv[0]);
        openos_close(sv[1]);
        return CAP_FAIL;
    }

    if (openos_recv(sv[1], &out, 1, 0) != 1 || out != ch) {
        openos_close(sv[0]);
        openos_close(sv[1]);
        return CAP_FAIL;
    }

    openos_close(sv[0]);
    openos_close(sv[1]);
    return CAP_PASS;
}

int main(int argc, char **argv)
{
    int failed = 0;
    int status;

    (void)argc;
    (void)argv;

    openos_printf("Chromium core capability test\n");
    openos_printf("target: mmap file-mmap mprotect brk thread shm eventfd socketpair poll time\n");

    status = test_uptime();
    print_result("monotonic uptime", status);
    failed += status == CAP_FAIL;

    status = test_mmap();
    print_result("anonymous mmap read/write/munmap", status);
    failed += status == CAP_FAIL;

    status = test_mmap_prot_flags();
    print_result("mmap prot/flags metadata", status);
    failed += status == CAP_FAIL;

    status = test_mmap_fixed();
    print_result("fixed-address mmap reservation", status);
    failed += status == CAP_FAIL;

    status = test_mprotect();
    print_result("mprotect page permission changes", status);
    failed += status == CAP_FAIL;

    status = test_file_mmap();
    print_result("file mmap private snapshot", status);
    failed += status == CAP_FAIL;

    status = test_sbrk();
    print_result("sbrk heap growth", status);
    failed += status == CAP_FAIL;

    status = test_thread();
    print_result("thread create shared address space", status);
    failed += status == CAP_FAIL;

    status = test_shm();
    print_result("shared memory double map coherence", status);
    failed += status == CAP_FAIL;

    status = test_eventfd();
    print_result("eventfd counter", status);
    failed += status == CAP_FAIL;

    status = test_socketpair_poll();
    print_result("socketpair send/recv/poll", status);
    failed += status == CAP_FAIL;

    if (failed) {
        openos_printf("Chromium core capability test: %d failure(s)\n", failed);
        return 1;
    }

    openos_printf("Chromium core capability test: all passed\n");
    return 0;
}
