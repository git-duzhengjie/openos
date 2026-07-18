/*
 * app_stack_selftest64.c — M10.9 app-stack selftest（8 阶段）
 *
 * 阶段：
 *   1) 初始化空栈
 *   2) manifest 表 3 条 + 查找
 *   3) push/pop 顺序
 *   4) overflow 保护
 *   5) 生命周期状态转移 (INIT→RESUMED→PAUSED→RESUMED→DESTROYED)
 *   6) LRU 排序（launched_seq 单调递增）
 *   7) top/at 边界
 *   8) 统计字段
 */
#include "../include/app_stack_selftest64.h"
#include "../include/early_console64.h"
#include "../../../kernel/include/app_stack.h"
#include "../../../kernel/include/app_manifest.h"

#include <stdint.h>

static void as_log(const char *s) { early_console64_write(s); }

static int as_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    uint32_t i = 0;
    while (a[i] && b[i]) { if (a[i]!=b[i]) return 0; i++; }
    return a[i]==0 && b[i]==0;
}

bool arch_x86_64_app_stack_selftest_run(void)
{
    bool ok = true;
    #define AS_CHK(cond, msg) do { if (!(cond)) { \
        as_log("[x86_64][app-stack-selftest] FAIL: " msg "\n"); ok = false; } } while (0)

    /* Stage 1: init empty */
    app_stack_init();
    app_manifest_init();
    AS_CHK(app_stack_depth() == 0,            "S1 depth != 0");
    AS_CHK(app_stack_is_empty() == 1,         "S1 not empty");
    AS_CHK(app_stack_top() == 0,              "S1 top not NULL");

    /* Stage 2: manifest table */
    AS_CHK(app_manifest_count() >= 3,         "S2 manifest count<3");
    const app_manifest_t *m_term  = app_manifest_find("terminal");
    const app_manifest_t *m_dmesg = app_manifest_find("dmesg");
    const app_manifest_t *m_hello = app_manifest_find("hello64");
    AS_CHK(m_term  != 0,                      "S2 terminal not found");
    AS_CHK(m_dmesg != 0,                      "S2 dmesg not found");
    AS_CHK(m_hello != 0,                      "S2 hello64 not found");
    AS_CHK(m_term  && as_streq(m_term->label,  "Terminal"),  "S2 terminal label");
    AS_CHK(m_dmesg && as_streq(m_dmesg->icon,  "LOG"),       "S2 dmesg icon");
    AS_CHK(m_hello && as_streq(m_hello->path,  "/bin/hello64.elf"), "S2 hello path");
    AS_CHK(app_manifest_find_index("no_such_app") == -1,     "S2 missing should be -1");

    /* Stage 3: push/pop */
    int i1 = app_stack_push("terminal", "builtin:terminal", app_manifest_find_index("terminal"), -1);
    AS_CHK(i1 == 0,                                          "S3 first push idx");
    AS_CHK(app_stack_depth() == 1,                           "S3 depth 1");
    int i2 = app_stack_push("dmesg", "builtin:dmesg", app_manifest_find_index("dmesg"), -1);
    AS_CHK(i2 == 1,                                          "S3 second push idx");
    AS_CHK(app_stack_depth() == 2,                           "S3 depth 2");
    const app_slot_t *tp = app_stack_top();
    AS_CHK(tp && as_streq(tp->name, "dmesg"),                "S3 top name");
    AS_CHK(app_stack_pop() == 0,                             "S3 pop ok");
    AS_CHK(app_stack_depth() == 1,                           "S3 depth after pop 1");
    tp = app_stack_top();
    AS_CHK(tp && as_streq(tp->name, "terminal"),             "S3 top after pop");
    AS_CHK(app_stack_pop() == 0,                             "S3 pop 2 ok");
    AS_CHK(app_stack_pop() == -1,                            "S3 pop empty must fail");

    /* Stage 4: overflow */
    app_stack_init();
    for (int i = 0; i < APP_STACK_MAX_DEPTH; i++) {
        AS_CHK(app_stack_push("terminal","builtin:terminal", 0, -1) >= 0, "S4 fill push");
    }
    AS_CHK(app_stack_depth() == APP_STACK_MAX_DEPTH,         "S4 depth == MAX");
    AS_CHK(app_stack_push("dmesg","builtin:dmesg",1,-1) == -1, "S4 overflow must be -1");
    AS_CHK(app_stack_get_stats()->overflow_drops >= 1,       "S4 overflow_drops++");

    /* Stage 5: lifecycle state */
    app_stack_init();
    app_stack_push("terminal","builtin:terminal", 0, -1);
    const app_slot_t *s1 = app_stack_top();
    AS_CHK(s1 && s1->state == APP_STATE_INIT,                "S5 first push INIT");
    app_stack_resume_top();
    s1 = app_stack_top();
    AS_CHK(s1 && s1->state == APP_STATE_RESUMED,             "S5 after resume RESUMED");
    app_stack_push("dmesg","builtin:dmesg", 1, -1);
    /* 旧顶应为 PAUSED，注意 app_stack_at(0) */
    const app_slot_t *s0 = app_stack_at(0);
    AS_CHK(s0 && s0->state == APP_STATE_PAUSED,              "S5 lower PAUSED");
    const app_slot_t *s2 = app_stack_top();
    AS_CHK(s2 && s2->state == APP_STATE_INIT,                "S5 new top INIT");
    app_stack_pop(); /* dmesg destroyed → terminal 自动 resume */
    s1 = app_stack_top();
    AS_CHK(s1 && s1->state == APP_STATE_RESUMED,             "S5 pop resumes prev");

    /* Stage 6: LRU snapshot 排序（seq 递减） */
    app_stack_init();
    app_stack_push("terminal","builtin:terminal", 0, -1); /* seq=1 */
    app_stack_push("dmesg",   "builtin:dmesg",    1, -1); /* seq=2 */
    app_stack_push("hello64", "/bin/hello64.elf", 2, -1); /* seq=3 */
    const app_slot_t *snap[APP_STACK_MAX_DEPTH];
    int n = app_stack_lru_snapshot(snap, APP_STACK_MAX_DEPTH);
    AS_CHK(n == 3,                                           "S6 snapshot count");
    AS_CHK(n >= 3 && as_streq(snap[0]->name, "hello64"),     "S6 LRU[0]=hello64");
    AS_CHK(n >= 3 && as_streq(snap[1]->name, "dmesg"),       "S6 LRU[1]=dmesg");
    AS_CHK(n >= 3 && as_streq(snap[2]->name, "terminal"),    "S6 LRU[2]=terminal");
    AS_CHK(snap[0]->launched_seq > snap[1]->launched_seq,    "S6 seq monotonic 0>1");
    AS_CHK(snap[1]->launched_seq > snap[2]->launched_seq,    "S6 seq monotonic 1>2");

    /* Stage 7: top/at 边界 */
    AS_CHK(app_stack_at(-1)   == 0,                          "S7 at -1 NULL");
    AS_CHK(app_stack_at(9999) == 0,                          "S7 at oob NULL");
    AS_CHK(app_stack_at(0)    != 0,                          "S7 at 0 valid");
    AS_CHK(app_stack_at(app_stack_depth()-1) == app_stack_top(), "S7 at depth-1 == top");

    /* Stage 8: stats */
    const app_stack_stats_t *st = app_stack_get_stats();
    AS_CHK(st->launches >= 3,                                "S8 launches>=3");
    AS_CHK(st->depth == 3,                                   "S8 depth==3");
    AS_CHK(st->max_depth_seen >= 3,                          "S8 max_depth_seen>=3");

    /* 清理，让后续 selftest 拿到干净状态 */
    app_stack_init();

    if (ok) as_log("[x86_64][app-stack-selftest] PASS\n");
    return ok;
    #undef AS_CHK
}
