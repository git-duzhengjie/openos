/* ============================================================
 * openos - i18n (Internationalization) - JSON-backed
 *
 * 译文以 JSON 文件为唯一数据源，不在代码中写死：
 *   - res/i18n/en.json  →  /etc/i18n/en.json  （英文，缺失 key 的回退语言）
 *   - res/i18n/zh.json  →  /etc/i18n/zh.json  （简体中文，启动默认语言）
 * 两个 JSON 于构建期经 _embed_elf.py 嵌入 initrd，运行时由本模块
 * 经 VFS 读取并解析填表。i18n_init() 在 GUI 启动早期调用，此时
 * initrd/FAT32 已挂载完毕（见 kernel64.c 时序）。
 *
 * 设计约定：
 *   - i18n_t() 恒返回非 NULL 字符串。
 *   - 请求语言缺失某 key 时回退 EN。
 *   - EN 也缺失（或 JSON 加载失败）时返回 "?<key>" 占位，便于 UI 排错。
 *   - JSON 解析失败不致命：表保持空，i18n_t 走占位分支，系统仍可运行。
 * ============================================================ */

#include "i18n.h"
#include "string.h"
#include "heap.h"
#include "core/fs/vfs.h"

/* JSON key 名 → 枚举下标映射（自动生成，顺序与 i18n_key_t 严格一致） */
#include "i18n_keys.inc"

static i18n_locale_t g_i18n_locale = I18N_LOCALE_ZH;
static int g_i18n_inited = 0;

/* 运行时译文表：指针指向解析后驻留在堆上的 UTF-8 字符串（含结尾 \0）。
 * 未加载 / 缺失的槽位为 NULL。 */
static const char *g_strings_en[I18N_KEY_COUNT];
static const char *g_strings_zh[I18N_KEY_COUNT];

/* 每种语言持有一块「原始文件缓冲」，解析出的字符串就地驻留其中（原地反转义、
 * 以 \0 分隔），故只需保留缓冲本身，无需逐条 kfree。 */
static char *g_buf_en = 0;
static char *g_buf_zh = 0;

static const char *const *k_locale_tables[I18N_LOCALE_COUNT] = {
    [I18N_LOCALE_EN] = g_strings_en,
    [I18N_LOCALE_ZH] = g_strings_zh,
};

/* ---- key 名 → 枚举下标 -------------------------------------------------- */
static int i18n_key_index(const char *name, int len) {
    for (int i = 0; i < I18N_KEY_COUNT; i++) {
        const char *k = k_key_names[i];
        if (!k) continue;
        int j = 0;
        while (j < len && k[j] && k[j] == name[j]) j++;
        if (j == len && k[j] == '\0') return i;
    }
    return -1;
}

/* ---- 迷你 JSON 解析器 ----------------------------------------------------
 * 仅支持扁平对象：{ "KEY": "VALUE", "KEY2": "VALUE2", ... }
 * - 就地解析：把字符串内容原地反转义并以 \0 收尾，table[idx] 指向缓冲内部。
 * - 支持转义：\" \\ \/ \n \t \r \b \f \uXXXX(仅 BMP，转 UTF-8)。
 * - 忽略对象外的空白；不支持嵌套对象/数组（i18n 数据无需）。
 * 返回填入的条目数。 */
static void put_utf8(char **dst, unsigned int cp) {
    char *d = *dst;
    if (cp < 0x80u) {
        *d++ = (char)cp;
    } else if (cp < 0x800u) {
        *d++ = (char)(0xC0u | (cp >> 6));
        *d++ = (char)(0x80u | (cp & 0x3Fu));
    } else {
        *d++ = (char)(0xE0u | (cp >> 12));
        *d++ = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        *d++ = (char)(0x80u | (cp & 0x3Fu));
    }
    *dst = d;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 就地解析一个 JSON 字符串：p 指向起始引号内的首字符（引号已跳过）。
 * 反转义后的内容写回 out（out 与源可重叠，因输出永不长于输入）。
 * 返回结束引号之后的位置；out_end 输出反转义结果的结束位置。 */
static char *parse_json_string(char *p, char *out, char **out_end) {
    char *o = out;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            char e = p[1];
            p += 2;
            switch (e) {
                case 'n': *o++ = '\n'; break;
                case 't': *o++ = '\t'; break;
                case 'r': *o++ = '\r'; break;
                case 'b': *o++ = '\b'; break;
                case 'f': *o++ = '\f'; break;
                case '/': *o++ = '/';  break;
                case '"': *o++ = '"';  break;
                case '\\': *o++ = '\\'; break;
                case 'u': {
                    int h0 = hexval(p[0]), h1 = hexval(p[1]);
                    int h2 = hexval(p[2]), h3 = hexval(p[3]);
                    if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                        unsigned int cp = (unsigned int)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                        put_utf8(&o, cp);
                        p += 4;
                    } else {
                        *o++ = 'u';
                    }
                    break;
                }
                default: *o++ = e; break;
            }
        } else {
            *o++ = *p++;
        }
    }
    if (*p == '"') p++;   /* 跳过结束引号 */
    *out_end = o;
    return p;
}

/* 解析整块 JSON，填充 table[]。就地修改 buf。返回填入条目数。 */
static int i18n_parse_json(char *buf, const char **table) {
    int filled = 0;
    char *p = buf;
    while (*p && *p != '{') p++;
    if (*p == '{') p++;
    for (;;) {
        while (*p && *p != '"' && *p != '}') p++;
        if (*p == '}' || *p == '\0') break;
        /* key */
        p++;                       /* skip opening quote */
        char *key = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p += 2; else p++;
        }
        int key_len = (int)(p - key);
        if (*p == '"') p++;
        /* colon */
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        /* value string */
        while (*p && *p != '"' && *p != '}') p++;
        if (*p != '"') { if (*p == '}') break; continue; }
        p++;                       /* skip opening quote of value */
        char *val = p;
        char *val_end = 0;
        p = parse_json_string(p, val, &val_end);
        *val_end = '\0';           /* 就地收尾 */
        int idx = i18n_key_index(key, key_len);
        if (idx >= 0) {
            table[idx] = val;
            filled++;
        }
    }
    return filled;
}

/* 读取整个文件到堆缓冲（自动追加结尾 \0）。成功返回缓冲指针，失败返回 0。 */
static char *i18n_read_file(const char *path) {
    inode_t st;
    if (vfs_stat(path, &st) != 0) return 0;
    uint32_t sz = (uint32_t)st.size;
    if (sz == 0 || sz > (1u << 20)) return 0;   /* 上限 1MB 防御 */
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    char *buf = (char *)kmalloc(sz + 1);
    if (!buf) { vfs_close(fd); return 0; }
    uint32_t got = 0;
    while (got < sz) {
        int n = vfs_read(fd, buf + got, sz - got);
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    vfs_close(fd);
    buf[got] = '\0';
    return buf;
}

extern void early_serial64_write(const char *s);
static void i18n_log_count(const char *path, int n) {
    char msg[64]; int i = 0;
    const char *pfx = "[i18n] loaded ";
    while (pfx[i]) { msg[i] = pfx[i]; i++; }
    /* n -> decimal (n < 1000) */
    char num[8]; int ni = 0; int v = n;
    if (v == 0) num[ni++] = '0';
    while (v > 0) { num[ni++] = (char)('0' + v % 10); v /= 10; }
    while (ni > 0) msg[i++] = num[--ni];
    const char *sfx = " entries from ";
    for (int j = 0; sfx[j]; j++) msg[i++] = sfx[j];
    for (int j = 0; path[j] && i < 62; j++) msg[i++] = path[j];
    msg[i++] = '\n'; msg[i] = '\0';
    early_serial64_write(msg);
}

static void i18n_load_locale(const char *path, const char **table, char **buf_slot) {
    char *buf = i18n_read_file(path);
    if (!buf) { early_serial64_write("[i18n] MISSING "); early_serial64_write(path); early_serial64_write("\n"); return; }
    *buf_slot = buf;             /* 持有缓冲，字符串就地驻留 */
    int n = i18n_parse_json(buf, table);
    i18n_log_count(path, n);
}

void i18n_init(void) {
    if (g_i18n_inited) return;
    g_i18n_locale = I18N_LOCALE_ZH;
    for (int i = 0; i < I18N_KEY_COUNT; i++) {
        g_strings_en[i] = 0;
        g_strings_zh[i] = 0;
    }
    i18n_load_locale("/etc/i18n/en.json", g_strings_en, &g_buf_en);
    i18n_load_locale("/etc/i18n/zh.json", g_strings_zh, &g_buf_zh);
    g_i18n_inited = 1;
}

int i18n_set_locale(i18n_locale_t locale) {
    if ((int)locale < 0 || (int)locale >= I18N_LOCALE_COUNT) return -1;
    g_i18n_locale = locale;
    return 0;
}

i18n_locale_t i18n_current(void) {
    return g_i18n_locale;
}

const char *i18n_t(i18n_key_t key) {
    if ((int)key < 0 || (int)key >= I18N_KEY_COUNT) {
        return "?key";
    }

    /* current locale lookup */
    i18n_locale_t loc = g_i18n_locale;
    if ((int)loc < 0 || (int)loc >= I18N_LOCALE_COUNT) {
        loc = I18N_LOCALE_EN;
    }
    const char *s = k_locale_tables[loc][key];
    if (s && s[0]) return s;

    /* fallback to EN */
    s = g_strings_en[key];
    if (s && s[0]) return s;

    /* table mismatch / JSON 未加载：返回可见占位 */
    const char *name = k_key_names[key];
    return name ? name : "?missing";
}
