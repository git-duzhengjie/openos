#include "openos.h"

#define CAP_PASS 0
#define CAP_FAIL 1
#define CAP_SKIP 2

#define MMAP_TEST_SIZE 4096
#define SHM_TEST_WORDS 16

static volatile int g_thread_done = 0;
static volatile int g_thread_value = 0;
static openos_pthread_mutex_t g_sync_mutex = 0;
static openos_pthread_cond_t g_sync_cond = 0;
static volatile int g_sync_ready = 0;
static volatile int g_tls_done = 0;
static volatile int g_tls_ok = 0;
static volatile unsigned int g_futex_word = 0;
static volatile int g_futex_waiting = 0;
static volatile int g_futex_done = 0;
static volatile int g_futex_ok = 0;

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

static int test_clock_gettime_monotonic(void)
{
    openos_timespec_t before;
    openos_timespec_t after;
    long long before_ns;
    long long after_ns;

    if (openos_clock_gettime(OPENOS_CLOCK_MONOTONIC, &before) != 0) {
        return CAP_FAIL;
    }
    if (before.tv_nsec < 0 || before.tv_nsec >= 1000000000ll) {
        return CAP_FAIL;
    }

    openos_sleep(2);

    if (openos_clock_gettime(OPENOS_CLOCK_MONOTONIC, &after) != 0) {
        return CAP_FAIL;
    }
    if (after.tv_nsec < 0 || after.tv_nsec >= 1000000000ll) {
        return CAP_FAIL;
    }

    before_ns = before.tv_sec * 1000000000ll + before.tv_nsec;
    after_ns = after.tv_sec * 1000000000ll + after.tv_nsec;
    return after_ns >= before_ns ? CAP_PASS : CAP_FAIL;
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

static int test_v8_memory_policy(void)
{
    unsigned int policy = openos_chromium_memory_policy();

    if ((policy & OPENOS_CHROMIUM_MEM_JITLESS_DEFAULT) == 0)
        return CAP_FAIL;
    if ((policy & OPENOS_CHROMIUM_MEM_EXEC_PROT_RESERVED) == 0)
        return CAP_FAIL;
    if ((policy & OPENOS_CHROMIUM_MEM_EXEC_MMAP_ENABLED) != 0 &&
        (policy & OPENOS_CHROMIUM_MEM_WX_ENFORCED) == 0)
        return CAP_FAIL;
    return CAP_PASS;
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

    if (openos_seek(fd, 0, SEEK_SET) != 0) {
        openos_close(fd);
        return CAP_FAIL;
    }
    mapped = (char *)openos_mmap_file(fd, 4096, OPENOS_PROT_READ,
                                      OPENOS_MAP_PRIVATE | OPENOS_MAP_FILE);
    if ((int)mapped == -1 || !mapped) {
        openos_close(fd);
        return CAP_FAIL;
    }
    if (mapped[0] != payload[0] || mapped[1] != payload[1]) {
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

static int test_filesystem_metadata(void)
{
    openos_stat_t bin_st;
    openos_stat_t file_st;
    openos_stat_t lstat_st;
    openos_stat_t fstat_st;
    openos_dirent_t entry;
    openos_DIR *dir;
    openos_dirent_t *de;
    int found_bin = 0;
    int found_self = 0;
    int fd;
    int i;

    if (openos_stat("/bin", &bin_st) != 0 ||
        (bin_st.mode & FS_DIR) != FS_DIR) {
        return CAP_FAIL;
    }

    if (openos_stat("/bin/chromiumcaptest", &file_st) != 0 ||
        (file_st.mode & FS_FILE) != FS_FILE || file_st.size == 0) {
        return CAP_FAIL;
    }

    if (openos_lstat("/bin/chromiumcaptest", &lstat_st) != 0 ||
        (lstat_st.mode & FS_FILE) != FS_FILE ||
        lstat_st.size != file_st.size) {
        return CAP_FAIL;
    }

    fd = openos_open("/bin/chromiumcaptest", O_RDONLY, 0);
    if (fd < 0) {
        return CAP_FAIL;
    }
    if (openos_fstat(fd, &fstat_st) != 0 ||
        (fstat_st.mode & FS_FILE) != FS_FILE ||
        fstat_st.size != file_st.size) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);

    for (i = 0; i < 64; ++i) {
        if (openos_readdir_path("/", i, &entry) <= 0)
            break;
        if (openos_strcmp(entry.name, "bin") == 0 &&
            (entry.mode & FS_DIR) == FS_DIR) {
            found_bin = 1;
        }
    }
    if (!found_bin) {
        return CAP_FAIL;
    }

    dir = openos_opendir("/bin");
    if (!dir) {
        return CAP_FAIL;
    }
    while ((de = openos_readdir(dir)) != 0) {
        if (openos_strcmp(de->name, "chromiumcaptest") == 0 &&
            (de->mode & FS_FILE) == FS_FILE && de->size == file_st.size) {
            found_self = 1;
        }
    }
    if (openos_closedir(dir) != 0) {
        return CAP_FAIL;
    }

    return found_self ? CAP_PASS : CAP_FAIL;
}

static int test_path_normalization(void)
{
    openos_stat_t direct_st;
    openos_stat_t norm_st;
    openos_stat_t rel_st;
    char cwd[64];

    if (openos_stat("/bin/chromiumcaptest", &direct_st) != 0 ||
        (direct_st.mode & FS_FILE) != FS_FILE || direct_st.size == 0) {
        return CAP_FAIL;
    }

    if (openos_stat("//bin/./../bin//chromiumcaptest", &norm_st) != 0 ||
        (norm_st.mode & FS_FILE) != FS_FILE ||
        norm_st.size != direct_st.size) {
        return CAP_FAIL;
    }

    if (openos_chdir("/bin/./../bin") != 0) {
        return CAP_FAIL;
    }
    if (openos_getcwd(cwd, sizeof(cwd)) != 0 ||
        openos_strcmp(cwd, "/bin") != 0) {
        openos_chdir("/");
        return CAP_FAIL;
    }

    if (openos_stat("./chromiumcaptest", &rel_st) != 0 ||
        (rel_st.mode & FS_FILE) != FS_FILE ||
        rel_st.size != direct_st.size) {
        openos_chdir("/");
        return CAP_FAIL;
    }

    if (openos_chdir("../bin/../../") != 0) {
        openos_chdir("/");
        return CAP_FAIL;
    }
    if (openos_getcwd(cwd, sizeof(cwd)) != 0 ||
        openos_strcmp(cwd, "/") != 0) {
        openos_chdir("/");
        return CAP_FAIL;
    }

    return CAP_PASS;
}

static int test_filesystem_mutations(void)
{
    const char *dir = "/tmp/chromiumcaptest_fs";
    const char *file = "/tmp/chromiumcaptest_fs/resource.pak";
    const char *hardlink = "/tmp/chromiumcaptest_fs/resource-hard.pak";
    const char *symlink = "/tmp/chromiumcaptest_fs/resource-link.pak";
    const char *payload = "pak-resource";
    openos_stat_t file_st;
    openos_stat_t hard_st;
    openos_stat_t sym_st;
    char linkbuf[64];
    int fd;
    int n;

    openos_unlink(symlink);
    openos_unlink(hardlink);
    openos_unlink(file);
    openos_rmdir(dir);

    if (openos_mkdir(dir, 0755) != 0) {
        return CAP_FAIL;
    }

    fd = openos_open(file, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        openos_rmdir(dir);
        return CAP_FAIL;
    }
    if (openos_write_fd(fd, payload, openos_strlen(payload)) != openos_strlen(payload)) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);

    if (openos_link(file, hardlink) != 0) {
        return CAP_FAIL;
    }
    if (openos_stat(file, &file_st) != 0 ||
        openos_stat(hardlink, &hard_st) != 0 ||
        file_st.ino != hard_st.ino || hard_st.nlinks < 2) {
        return CAP_FAIL;
    }

    if (openos_symlink(file, symlink) != 0) {
        return CAP_FAIL;
    }
    openos_memset(linkbuf, 0, sizeof(linkbuf));
    n = openos_readlink(symlink, linkbuf, sizeof(linkbuf) - 1);
    if (n != openos_strlen(file) || openos_strcmp(linkbuf, file) != 0) {
        return CAP_FAIL;
    }
    if (openos_lstat(symlink, &sym_st) != 0 ||
        (sym_st.mode & FS_SYMLINK) != FS_SYMLINK) {
        return CAP_FAIL;
    }
    if (openos_stat(symlink, &sym_st) != 0 ||
        sym_st.ino != file_st.ino || (sym_st.mode & FS_FILE) != FS_FILE) {
        return CAP_FAIL;
    }

    if (openos_unlink(file) != 0) {
        return CAP_FAIL;
    }
    if (openos_stat(hardlink, &hard_st) != 0 ||
        hard_st.ino != file_st.ino || hard_st.nlinks < 1) {
        return CAP_FAIL;
    }

    if (openos_unlink(symlink) != 0 || openos_unlink(hardlink) != 0 ||
        openos_rmdir(dir) != 0) {
        return CAP_FAIL;
    }

    return CAP_PASS;
}

static int test_browser_data_directories(void)
{
    const char *dirs[] = {
        OPENOS_BROWSER_PROFILE_DIR,
        OPENOS_BROWSER_CACHE_DIR,
        OPENOS_BROWSER_COOKIE_DIR,
        OPENOS_BROWSER_CERT_DIR,
        OPENOS_BROWSER_PROFILES_DIR,
        OPENOS_BROWSER_DOWNLOAD_DIR,
    };
    const char *cache_file = OPENOS_BROWSER_CACHE_DIR "/chromiumcaptest-cache.bin";
    const char *profile_dir = OPENOS_BROWSER_PROFILES_DIR "/Default";
    const char *prefs_file = OPENOS_BROWSER_PROFILES_DIR "/Default/Preferences";
    const char *prefs_payload = "{\"homepage\":\"about:blank\"}";
    openos_stat_t st;
    openos_dirent_t entry;
    int found_default = 0;
    int fd;
    int i;

    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); ++i) {
        if (openos_stat(dirs[i], &st) != 0 || (st.mode & FS_DIR) != FS_DIR) {
            return CAP_FAIL;
        }
    }

    openos_unlink(cache_file);
    fd = openos_open(cache_file, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        return CAP_FAIL;
    }
    if (openos_write_fd(fd, "cache", 5) != 5) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);
    if (openos_stat(cache_file, &st) != 0 ||
        (st.mode & FS_FILE) != FS_FILE || st.size != 5) {
        return CAP_FAIL;
    }
    if (openos_unlink(cache_file) != 0) {
        return CAP_FAIL;
    }

    if (openos_mkdir(profile_dir, 0755) != 0 &&
        openos_stat(profile_dir, &st) != 0) {
        return CAP_FAIL;
    }
    fd = openos_open(prefs_file, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        return CAP_FAIL;
    }
    if (openos_write_fd(fd, prefs_payload, openos_strlen(prefs_payload)) !=
        openos_strlen(prefs_payload)) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);

    for (i = 0; i < 64; ++i) {
        if (openos_readdir_path(OPENOS_BROWSER_PROFILES_DIR, i, &entry) <= 0)
            break;
        if (openos_strcmp(entry.name, "Default") == 0 &&
            (entry.mode & FS_DIR) == FS_DIR) {
            found_default = 1;
        }
    }

    if (!found_default) {
        return CAP_FAIL;
    }
    if (openos_stat(prefs_file, &st) != 0 ||
        (st.mode & FS_FILE) != FS_FILE ||
        st.size != openos_strlen(prefs_payload)) {
        return CAP_FAIL;
    }

    if (openos_unlink(prefs_file) != 0 || openos_rmdir(profile_dir) != 0) {
        return CAP_FAIL;
    }

    return CAP_PASS;
}

static int test_browser_resource_paths(void)
{
    const char *dirs[] = {
        OPENOS_RESOURCE_DIR,
        OPENOS_BROWSER_RESOURCE_DIR,
        OPENOS_BROWSER_PAK_DIR,
    };
    const char *pak = OPENOS_BROWSER_PAK_DIR "/chromiumcaptest.pak";
    const char *payload = "PAK\0OpenOS Chromium resource payload";
    char buf[64];
    openos_stat_t st;
    openos_dirent_t entry;
    int found_pak = 0;
    int fd;
    int i;
    int payload_len = 35;

    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); ++i) {
        if (openos_stat(dirs[i], &st) != 0 || (st.mode & FS_DIR) != FS_DIR) {
            return CAP_FAIL;
        }
    }

    openos_unlink(pak);
    fd = openos_open(pak, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        return CAP_FAIL;
    }
    if (openos_write_fd(fd, payload, payload_len) != payload_len) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);

    if (openos_stat(pak, &st) != 0 ||
        (st.mode & FS_FILE) != FS_FILE || st.size != payload_len) {
        return CAP_FAIL;
    }

    fd = openos_open(pak, O_RDONLY, 0);
    if (fd < 0) {
        return CAP_FAIL;
    }
    openos_memset(buf, 0, sizeof(buf));
    if (openos_read(fd, buf, sizeof(buf)) != payload_len) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);
    if (buf[0] != 'P' || buf[1] != 'A' || buf[2] != 'K' || buf[3] != 0 ||
        buf[4] != 'O' || buf[payload_len - 1] != 'd') {
        return CAP_FAIL;
    }

    for (i = 0; i < 64; ++i) {
        if (openos_readdir_path(OPENOS_BROWSER_PAK_DIR, i, &entry) <= 0)
            break;
        if (openos_strcmp(entry.name, "chromiumcaptest.pak") == 0 &&
            (entry.mode & FS_FILE) == FS_FILE && entry.size == payload_len) {
            found_pak = 1;
        }
    }
    if (!found_pak) {
        return CAP_FAIL;
    }

    if (openos_unlink(pak) != 0) {
        return CAP_FAIL;
    }

    return CAP_PASS;
}

static int test_sparse_seek_file(void)
{
    const char *path = "/tmp/chromiumcaptest_sparse.bin";
    const int hole_offset = 8192;
    char buf[8];
    openos_stat_t st;
    int fd;

    openos_unlink(path);
    fd = openos_open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        return CAP_FAIL;
    }

    if (openos_write_fd(fd, "HEAD", 4) != 4) {
        openos_close(fd);
        return CAP_FAIL;
    }
    if (openos_seek(fd, hole_offset, SEEK_SET) != hole_offset) {
        openos_close(fd);
        return CAP_FAIL;
    }
    if (openos_write_fd(fd, "TAIL", 4) != 4) {
        openos_close(fd);
        return CAP_FAIL;
    }
    if (openos_seek(fd, -4, SEEK_END) != hole_offset) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_memset(buf, 0, sizeof(buf));
    if (openos_read(fd, buf, 4) != 4 ||
        buf[0] != 'T' || buf[1] != 'A' || buf[2] != 'I' || buf[3] != 'L') {
        openos_close(fd);
        return CAP_FAIL;
    }
    if (openos_seek(fd, 4, SEEK_SET) != 4) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_memset(buf, 0x7f, sizeof(buf));
    if (openos_read(fd, buf, 4) != 4 ||
        buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0) {
        openos_close(fd);
        return CAP_FAIL;
    }
    openos_close(fd);

    if (openos_stat(path, &st) != 0 ||
        (st.mode & FS_FILE) != FS_FILE || st.size != hole_offset + 4) {
        return CAP_FAIL;
    }
    if (openos_unlink(path) != 0) {
        return CAP_FAIL;
    }

    return CAP_PASS;
}

static int test_statfs(void)
{
    const char *path = "/tmp/chromiumcaptest_statfs.tmp";
    openos_statfs_t root_st;
    openos_statfs_t fd_st;
    int fd;

    openos_memset(&root_st, 0, sizeof(root_st));
    openos_memset(&fd_st, 0, sizeof(fd_st));

    if (openos_statfs("/", &root_st) < 0)
        return CAP_FAIL;
    if (root_st.f_bsize == 0 || root_st.f_namelen == 0)
        return CAP_FAIL;
    if (root_st.f_bavail > root_st.f_blocks || root_st.f_bfree > root_st.f_blocks)
        return CAP_FAIL;
    if (root_st.f_ffree > root_st.f_files)
        return CAP_FAIL;

    openos_unlink(path);
    fd = openos_open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0)
        return CAP_FAIL;
    if (openos_fstatfs(fd, &fd_st) < 0) {
        openos_close(fd);
        openos_unlink(path);
        return CAP_FAIL;
    }
    openos_close(fd);
    openos_unlink(path);

    if (fd_st.f_bsize != root_st.f_bsize)
        return CAP_FAIL;
    if (fd_st.f_namelen != root_st.f_namelen)
        return CAP_FAIL;
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

static void sync_worker(void *arg)
{
    (void)arg;
    if (openos_pthread_mutex_lock(&g_sync_mutex) == 0) {
        g_sync_ready = 1;
        openos_pthread_cond_signal(&g_sync_cond);
        openos_pthread_mutex_unlock(&g_sync_mutex);
    }
    openos_thread_exit(0);
}

static void tls_worker(void *arg)
{
    int *worker_tls = (int *)arg;
    if (openos_tls_set(worker_tls) == 0 && openos_tls_get() == worker_tls)
        g_tls_ok = 1;
    g_tls_done = 1;
    openos_thread_exit(0);
}

static int test_tls(void)
{
    int main_tls = 0x1111;
    int worker_tls = 0x2222;
    openos_thread_t tid;
    int spin = 0;

    g_tls_done = 0;
    g_tls_ok = 0;
    if (openos_tls_set(&main_tls) != 0)
        return CAP_FAIL;
    if (openos_tls_get() != &main_tls)
        return CAP_FAIL;
    if (openos_thread_create(&tid, tls_worker, &worker_tls) != 0)
        return CAP_FAIL;
    while (!g_tls_done && spin++ < 100000)
        openos_yield();
    if (!g_tls_done || !g_tls_ok)
        return CAP_FAIL;
    if (openos_tls_get() != &main_tls)
        return CAP_FAIL;
    return CAP_PASS;
}

static int test_pthread_sync(void)
{
    openos_pthread_t tid;
    int loops = 0;

    g_sync_ready = 0;
    if (openos_pthread_mutex_init(&g_sync_mutex) != 0)
        return CAP_FAIL;
    if (openos_pthread_cond_init(&g_sync_cond) != 0) {
        openos_pthread_mutex_destroy(&g_sync_mutex);
        return CAP_FAIL;
    }
    if (openos_pthread_mutex_lock(&g_sync_mutex) != 0) {
        openos_pthread_cond_destroy(&g_sync_cond);
        openos_pthread_mutex_destroy(&g_sync_mutex);
        return CAP_FAIL;
    }
    if (openos_pthread_create(&tid, sync_worker, 0) != 0) {
        openos_pthread_mutex_unlock(&g_sync_mutex);
        openos_pthread_cond_destroy(&g_sync_cond);
        openos_pthread_mutex_destroy(&g_sync_mutex);
        return CAP_FAIL;
    }
    while (!g_sync_ready && loops++ < 64) {
        if (openos_pthread_cond_wait(&g_sync_cond, &g_sync_mutex) != 0) {
            openos_pthread_mutex_unlock(&g_sync_mutex);
            (void)tid;
            openos_pthread_cond_destroy(&g_sync_cond);
            openos_pthread_mutex_destroy(&g_sync_mutex);
            return CAP_FAIL;
        }
    }
    openos_pthread_mutex_unlock(&g_sync_mutex);
    (void)tid;
    openos_pthread_cond_destroy(&g_sync_cond);
    openos_pthread_mutex_destroy(&g_sync_mutex);
    return g_sync_ready == 1 ? CAP_PASS : CAP_FAIL;
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
    openos_shm_info_t info;
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
    if (openos_shm_info(&shm, &info) != 0 || info.refcount != 2 || info.size < 4096u) {
        openos_shm_destroy(&shm);
        return CAP_FAIL;
    }
    if (openos_shm_destroy(&shm) == 0) {
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

static void futex_worker(void *arg)
{
    (void)arg;
    g_futex_waiting = 1;
    if (openos_futex_wait(&g_futex_word, 0) == 0 && g_futex_word == 1)
        g_futex_ok = 1;
    g_futex_done = 1;
    openos_thread_exit(0);
}

static int test_futex(void)
{
    openos_thread_t tid;
    int spin = 0;
    int woke;

    g_futex_word = 0;
    g_futex_waiting = 0;
    g_futex_done = 0;
    g_futex_ok = 0;

    if (openos_thread_create(&tid, futex_worker, 0) != 0)
        return CAP_FAIL;
    while (!g_futex_waiting && spin++ < 100000)
        openos_yield();
    if (!g_futex_waiting)
        return CAP_FAIL;
    for (spin = 0; spin < 64; ++spin)
        openos_yield();

    g_futex_word = 1;
    woke = openos_futex_wake(&g_futex_word, 1);
    if (woke != 1)
        return CAP_FAIL;

    spin = 0;
    while (!g_futex_done && spin++ < 100000)
        openos_yield();
    (void)tid;
    return (g_futex_done && g_futex_ok) ? CAP_PASS : CAP_FAIL;
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

static int test_message_queue(void)
{
    openos_mq_t mq;
    char buf[32];
    int ret;

    if (openos_mq_create(&mq) != 0) {
        return CAP_FAIL;
    }

    ret = openos_mq_send(&mq, "hello", 6);
    if (ret != 6) {
        openos_mq_destroy(&mq);
        return CAP_FAIL;
    }

    ret = openos_mq_recv(&mq, buf, sizeof(buf));
    if (ret != 6 || buf[0] != 'h' || buf[1] != 'e' || buf[2] != 'l' ||
        buf[3] != 'l' || buf[4] != 'o' || buf[5] != 0) {
        openos_mq_destroy(&mq);
        return CAP_FAIL;
    }

    ret = openos_mq_send(&mq, "second", 7);
    if (ret != 7) {
        openos_mq_destroy(&mq);
        return CAP_FAIL;
    }

    ret = openos_mq_recv(&mq, buf, 4);
    if (ret != 4 || buf[0] != 's' || buf[1] != 'e' ||
        buf[2] != 'c' || buf[3] != 'o') {
        openos_mq_destroy(&mq);
        return CAP_FAIL;
    }

    return openos_mq_destroy(&mq) == 0 && mq == 0 ? CAP_PASS : CAP_FAIL;
}

static int test_service_channel(void)
{
    openos_service_channel_t channel;
    openos_service_message_t request;
    openos_service_message_t server_request;
    openos_service_message_t reply;
    openos_service_message_t client_reply;

    if (openos_service_channel_create(&channel) != 0) {
        return CAP_FAIL;
    }

    openos_service_message_init(&request, 0x4348524f, 7, 1234, "ping", 5);
    if (openos_service_send_message(channel.client_fd, &request) != 0) {
        openos_service_channel_close(&channel);
        return CAP_FAIL;
    }

    if (openos_service_receive_request(&channel, &server_request) != 0) {
        openos_service_channel_close(&channel);
        return CAP_FAIL;
    }
    if (server_request.service != request.service ||
        server_request.opcode != request.opcode ||
        server_request.seq != request.seq ||
        server_request.length != 5 ||
        server_request.payload[0] != 'p' ||
        server_request.payload[1] != 'i' ||
        server_request.payload[2] != 'n' ||
        server_request.payload[3] != 'g' ||
        server_request.payload[4] != 0) {
        openos_service_channel_close(&channel);
        return CAP_FAIL;
    }

    openos_service_message_init(&reply, server_request.service,
                                server_request.opcode + 1,
                                server_request.seq, "pong", 5);
    reply.status = OPENOS_SERVICE_STATUS_OK;
    if (openos_service_send_reply(&channel, &reply) != 0) {
        openos_service_channel_close(&channel);
        return CAP_FAIL;
    }

    if (openos_service_recv_message(channel.client_fd, &client_reply) != 0) {
        openos_service_channel_close(&channel);
        return CAP_FAIL;
    }

    if (client_reply.service != request.service ||
        client_reply.opcode != 8 ||
        client_reply.seq != request.seq ||
        client_reply.status != OPENOS_SERVICE_STATUS_OK ||
        client_reply.length != 5 ||
        client_reply.payload[0] != 'p' ||
        client_reply.payload[1] != 'o' ||
        client_reply.payload[2] != 'n' ||
        client_reply.payload[3] != 'g' ||
        client_reply.payload[4] != 0) {
        openos_service_channel_close(&channel);
        return CAP_FAIL;
    }

    return openos_service_channel_close(&channel) == 0 &&
           channel.client_fd == -1 && channel.server_fd == -1
               ? CAP_PASS
               : CAP_FAIL;
}

static int test_spawn_argv_env_wait(void)
{
    char *argv[] = { (char *)"/bin/argtest", (char *)"alpha", (char *)"beta", 0 };
    char *envp[] = { (char *)"OPENOS_CAP=chromium", (char *)"OPENOS_MODE=test", 0 };
    int status = -1;
    int pid = openos_spawn_env("/bin/argtest", argv, envp);
    if (pid <= 0) {
        openos_puts("spawn_env failed");
        return CAP_FAIL;
    }
    if (openos_waitpid(pid, &status, 0) != pid) {
        openos_puts("waitpid did not reap spawned child");
        return CAP_FAIL;
    }
    if (status != 0) {
        openos_puts("spawned argv child exited nonzero");
        return CAP_FAIL;
    }

    argv[0] = (char *)"/bin/envtest";
    pid = openos_spawn_env("/bin/envtest", argv, envp);
    if (pid <= 0) {
        openos_puts("spawn_env env child failed");
        return CAP_FAIL;
    }
    status = -1;
    if (openos_waitpid(pid, &status, 0) != pid) {
        openos_puts("waitpid did not reap env child");
        return CAP_FAIL;
    }
    if (status != 0) {
        openos_puts("spawned env child exited nonzero");
        return CAP_FAIL;
    }
    return CAP_PASS;
}

static int test_fork_pipe_fd_inheritance(void)
{
    int pipefd[2];
    char buf[16];
    int status = -1;
    int pid;
    int n;

    if (openos_pipe(pipefd) != 0) {
        openos_puts("pipe create failed");
        return CAP_FAIL;
    }

    pid = openos_fork();
    if (pid < 0) {
        openos_puts("fork failed");
        openos_close(pipefd[0]);
        openos_close(pipefd[1]);
        return CAP_FAIL;
    }

    if (pid == 0) {
        const char *msg = "ipc-ok";
        openos_close(pipefd[0]);
        if (openos_write_fd(pipefd[1], msg, 6) != 6) {
            openos_exit(31);
        }
        openos_close(pipefd[1]);
        openos_exit(0);
    }

    openos_close(pipefd[1]);
    n = openos_read(pipefd[0], buf, 6);
    openos_close(pipefd[0]);
    if (n != 6) {
        openos_puts("parent failed to read inherited pipe fd");
        return CAP_FAIL;
    }
    buf[6] = 0;
    if (openos_strcmp(buf, "ipc-ok") != 0) {
        openos_puts("pipe payload mismatch");
        return CAP_FAIL;
    }
    if (openos_waitpid(pid, &status, 0) != pid) {
        openos_puts("waitpid fork child failed");
        return CAP_FAIL;
    }
    if (status != 0) {
        openos_puts("fork child exited nonzero");
        return CAP_FAIL;
    }
    return CAP_PASS;
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
    openos_printf("target: mmap file-mmap fs-metadata path-normalization fs-mutations browser-dirs resource-pak sparse-seek mprotect v8-memory-policy brk thread tls pthread-sync futex shm eventfd message-queue service-channel socketpair poll time spawn fork pipe fd argv env\n");

    status = test_uptime();
    print_result("monotonic uptime", status);
    failed += status == CAP_FAIL;

    status = test_clock_gettime_monotonic();
    print_result("clock_gettime monotonic timespec", status);
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

    status = test_v8_memory_policy();
    print_result("V8 executable memory policy", status);
    failed += status == CAP_FAIL;

    status = test_file_mmap();
    print_result("file mmap private snapshot/copy-on-write", status);
    failed += status == CAP_FAIL;

    status = test_filesystem_metadata();
    print_result("filesystem stat/fstat/lstat/readdir", status);
    failed += status == CAP_FAIL;

    status = test_path_normalization();
    print_result("path normalization and cwd", status);
    failed += status == CAP_FAIL;

    status = test_filesystem_mutations();
    print_result("filesystem link/symlink/unlink/rmdir", status);
    failed += status == CAP_FAIL;

    status = test_browser_data_directories();
    print_result("browser cache/profile directories", status);
    failed += status == CAP_FAIL;

    status = test_browser_resource_paths();
    print_result("browser resource pak paths", status);
    failed += status == CAP_FAIL;

    status = test_sparse_seek_file();
    print_result("sparse file seek/read/write", status);
    failed += status == CAP_FAIL;

    status = test_statfs();
    print_result("statfs/fstatfs filesystem info", status);
    failed += status == CAP_FAIL;

    status = test_sbrk();
    print_result("sbrk heap growth", status);
    failed += status == CAP_FAIL;

    status = test_thread();
    print_result("thread create shared address space", status);
    failed += status == CAP_FAIL;

    status = test_tls();
    print_result("thread-local storage base", status);
    failed += status == CAP_FAIL;

    status = test_pthread_sync();
    print_result("pthread-like mutex/cond synchronization", status);
    failed += status == CAP_FAIL;

    status = test_futex();
    print_result("futex wait/wake synchronization", status);
    failed += status == CAP_FAIL;

    status = test_shm();
    print_result("shared memory double map coherence", status);
    failed += status == CAP_FAIL;

    status = test_eventfd();
    print_result("eventfd counter", status);
    failed += status == CAP_FAIL;

    status = test_message_queue();
    print_result("message queue send/recv/truncate", status);
    failed += status == CAP_FAIL;

    status = test_service_channel();
    print_result("service channel request/reply", status);
    failed += status == CAP_FAIL;

    status = test_socketpair_poll();
    print_result("socketpair send/recv/poll", status);
    failed += status == CAP_FAIL;

    status = test_spawn_argv_env_wait();
    print_result("spawn argv env waitpid", status);
    failed += status == CAP_FAIL;

    status = test_fork_pipe_fd_inheritance();
    print_result("fork pipe fd inheritance", status);
    failed += status == CAP_FAIL;

    if (failed) {
        openos_printf("Chromium core capability test: %d failure(s)\n", failed);
        return 1;
    }

    openos_printf("Chromium core capability test: all passed\n");
    return 0;
}
