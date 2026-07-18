/* ============================================================================
 * app_manifest.c — M10.2 应用清单（硬编码 3 条内建应用）
 * ============================================================================ */
#include "app_manifest.h"

#include <stdint.h>

static app_manifest_t g_table[APP_MANIFEST_MAX_ENTRIES];
static int            g_count;
static int            g_inited;

static void am_memzero_(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void am_strcpy_(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (src) {
        while (i + 1 < cap && src[i]) { dst[i] = src[i]; i++; }
    }
    dst[i] = 0;
}

static int am_streq_(const char *a, const char *b) {
    if (!a || !b) return 0;
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static void am_add_(const char *name, const char *label, const char *path, const char *icon,
                    uint32_t perm, uint32_t hooks, int is_builtin) {
    if (g_count >= APP_MANIFEST_MAX_ENTRIES) return;
    app_manifest_t *m = &g_table[g_count];
    am_memzero_(m, sizeof(*m));
    m->used = 1;
    am_strcpy_(m->name,  name,  APP_MANIFEST_NAME_MAX);
    am_strcpy_(m->label, label, APP_MANIFEST_LABEL_MAX);
    am_strcpy_(m->path,  path,  APP_MANIFEST_PATH_MAX);
    am_strcpy_(m->icon,  icon,  APP_MANIFEST_ICON_MAX);
    m->perm       = perm;
    m->hooks      = hooks;
    m->is_builtin = is_builtin;
    g_count++;
}

void app_manifest_init(void) {
    if (g_inited) return;
    am_memzero_(g_table, sizeof(g_table));
    g_count = 0;

    /* 内建 1: terminal */
    am_add_("terminal", "Terminal", "builtin:terminal", ">_",
            APP_PERM_INPUT | APP_PERM_GFX | APP_PERM_KLOG,
            APP_HOOK_ON_LAUNCH | APP_HOOK_ON_RESUME | APP_HOOK_ON_PAUSE | APP_HOOK_ON_DESTROY,
            /*is_builtin=*/1);

    /* 内建 2: dmesg */
    am_add_("dmesg", "Kernel Log", "builtin:dmesg", "LOG",
            APP_PERM_KLOG | APP_PERM_GFX,
            APP_HOOK_ON_LAUNCH | APP_HOOK_ON_RESUME | APP_HOOK_ON_PAUSE | APP_HOOK_ON_DESTROY,
            /*is_builtin=*/1);

    /* 内建 3: hello64 (ELF via /bin/hello64) */
    am_add_("hello64", "Hello", "/bin/hello64.elf", "HI",
            APP_PERM_GFX | APP_PERM_KLOG,
            APP_HOOK_ON_LAUNCH | APP_HOOK_ON_DESTROY,
            /*is_builtin=*/0);

    g_inited = 1;
}

int app_manifest_count(void) { return g_count; }

const app_manifest_t *app_manifest_at(int index) {
    if (index < 0 || index >= g_count) return (const app_manifest_t *)0;
    if (!g_table[index].used) return (const app_manifest_t *)0;
    return &g_table[index];
}

const app_manifest_t *app_manifest_find(const char *name) {
    if (!name) return (const app_manifest_t *)0;
    for (int i = 0; i < g_count; i++) {
        if (!g_table[i].used) continue;
        if (am_streq_(g_table[i].name, name)) return &g_table[i];
    }
    return (const app_manifest_t *)0;
}

int app_manifest_find_index(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_count; i++) {
        if (!g_table[i].used) continue;
        if (am_streq_(g_table[i].name, name)) return i;
    }
    return -1;
}
