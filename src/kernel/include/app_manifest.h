/* ============================================================================
 * app_manifest.h — M10.2 应用清单（硬编码 3 条内建应用）
 *
 * 手机式应用需要清单描述：name / icon / permission / lifecycle-hooks。
 * 本模块只做纯逻辑接口 + 硬编码表，真解析（TOML/JSON/ELF note）留 M11。
 *
 * 内建 3 条：
 *   0. terminal — 终端（触屏 GUI 已有 terminal 组件）
 *   1. dmesg    — 内核日志查看器（复用 klog）
 *   2. hello64  — Hello World 演示（复用 usermode64.S 的 hello 用户程序）
 * ============================================================================ */
#ifndef OPENOS_APP_MANIFEST_H
#define OPENOS_APP_MANIFEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MANIFEST_MAX_ENTRIES 8
#define APP_MANIFEST_NAME_MAX    32
#define APP_MANIFEST_LABEL_MAX   32
#define APP_MANIFEST_PATH_MAX    64
#define APP_MANIFEST_ICON_MAX    8   /* 迷你 icon: 4 字符 UTF-8 或 emoji 占位 */

/* 权限位（位掩码；此期只做展示，真授权 M11） */
#define APP_PERM_NET     (1u << 0)
#define APP_PERM_FS      (1u << 1)
#define APP_PERM_INPUT   (1u << 2)
#define APP_PERM_GFX     (1u << 3)
#define APP_PERM_KLOG    (1u << 4)

/* 生命周期钩子标志：manifest 可声明该 app 感兴趣的钩子。
 * 真实调用点在 M10.4 launcher，本模块只暴露标志。 */
#define APP_HOOK_ON_LAUNCH  (1u << 0)
#define APP_HOOK_ON_RESUME  (1u << 1)
#define APP_HOOK_ON_PAUSE   (1u << 2)
#define APP_HOOK_ON_DESTROY (1u << 3)

typedef struct app_manifest_s {
    int      used;                              /* 该条是否有效 */
    char     name[APP_MANIFEST_NAME_MAX];       /* 内部唯一名（app_launch 查表用） */
    char     label[APP_MANIFEST_LABEL_MAX];     /* 显示名 */
    char     path[APP_MANIFEST_PATH_MAX];       /* 内建："builtin:xxx"；ELF："/bin/xxx.elf" */
    char     icon[APP_MANIFEST_ICON_MAX];       /* 图标占位（emoji/ASCII） */
    uint32_t perm;                              /* 权限掩码 */
    uint32_t hooks;                             /* 钩子掩码 */
    int      is_builtin;                        /* 1 = 内建 GUI 组件（terminal/dmesg 等） */
} app_manifest_t;

/* 初始化：注册硬编码 3 条。幂等，可反复调用。 */
void app_manifest_init(void);

/* 数量 */
int app_manifest_count(void);

/* 按索引取；index 越界返回 NULL */
const app_manifest_t *app_manifest_at(int index);

/* 按 name 查表；找不到返回 NULL */
const app_manifest_t *app_manifest_find(const char *name);

/* 按 name 找 index；找不到返回 -1 */
int app_manifest_find_index(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* OPENOS_APP_MANIFEST_H */
