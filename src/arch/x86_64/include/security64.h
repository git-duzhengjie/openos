#ifndef OPENOS_ARCH_X86_64_SECURITY64_H
#define OPENOS_ARCH_X86_64_SECURITY64_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ *
 * M6.4 安全加固 (security hardening)
 *
 * 子项:
 *   M6.4.1  CPU 能力探测 (CPUID leaf 7 / 0x80000001)
 *   M6.4.2  W^X 强制 (kernel .text RX, .data/.rodata NX)
 *   M6.4.3  SMEP  (CR4.bit20, 禁止内核执行用户页)
 *   M6.4.4  SMAP  (CR4.bit21, 禁止内核越权访问用户页 + STAC/CLAC)
 *   M6.4.5  栈保护 canary + ASLR (用户栈/mmap 基址随机化)
 * ------------------------------------------------------------------ */

/* CR4 位 */
#define OPENOS_X86_64_CR4_SMEP (1ULL << 20)
#define OPENOS_X86_64_CR4_SMAP (1ULL << 21)

/* RFLAGS.AC (bit18) —— SMAP 临时放行 */
#define OPENOS_X86_64_RFLAGS_AC (1ULL << 18)

/* CPU 安全能力位 (探测结果) */
typedef struct openos_x86_64_security_caps {
    bool nx;         /* CPUID 0x80000001 EDX bit20  —— NX/XD 位可用 */
    bool smep;       /* CPUID 7:0 EBX bit7                          */
    bool smap;       /* CPUID 7:0 EBX bit20                         */
    bool umip;       /* CPUID 7:0 ECX bit2  (记录用, 暂不启用)      */

    bool smep_enabled;   /* 实际是否已置 CR4.SMEP */
    bool smap_enabled;   /* 实际是否已置 CR4.SMAP */
} openos_x86_64_security_caps_t;

/* 探测 CPU 安全能力 (幂等) */
const openos_x86_64_security_caps_t *arch_x86_64_security_probe(void);

/* 依据探测结果启用 SMEP (幂等; 无能力则安全跳过)。
 * SMEP 只禁止内核执行用户页, 正常内核不会这么做, 上线无需改造其它代码。 */
void arch_x86_64_security_enable_smep(void);

/* 启用 SMAP (幂等; 无能力则安全跳过)。
 * ⚠️ 前置条件: 所有内核访问用户内存的路径必须用 stac()/clac() 包裹,
 * 否则会立即触发 #PF。故与 SMEP 分离, 单独按 M6.4.4 上线。 */
void arch_x86_64_security_enable_smap(void);

/* M6.4.2 W^X 强制: 内核 .text=RX, .rodata=RO-NX, .data/.bss=RW-NX。
 * 内部先保证 EFER.NXE。无 NX 能力则安全跳过。必须在 vmm_init 后调用。 */
void arch_x86_64_security_enforce_wxorx(void);

/* 获取最近一次探测结果 (未探测则先探测) */
const openos_x86_64_security_caps_t *arch_x86_64_security_caps(void);

/* ---- SMAP 临时放行: 内核合法访问用户内存时包裹 ---- */
static inline void arch_x86_64_stac(void) {
    __asm__ __volatile__("stac" ::: "cc", "memory");
}
static inline void arch_x86_64_clac(void) {
    __asm__ __volatile__("clac" ::: "cc", "memory");
}

/* ---- 栈保护 canary ---- */
extern uint64_t __stack_chk_guard;
void arch_x86_64_stack_guard_init(void);

/* ---- M6.4.6 ASLR: 用户栈基址随机化 ----
 * 返回一个 16 字节对齐、上限受控 (<= 512 页, 即 2MiB) 的随机 gap 字节数,
 * 供 seed_user_stack 从栈顶向下预留, 使每次启动的用户栈虚拟位置不同,
 * 抵御依赖固定栈地址的 ROP/信息泄露利用链。TSC 熵驱动, 幂等安全。 */
uint64_t arch_x86_64_aslr_stack_gap(void);

#endif /* OPENOS_ARCH_X86_64_SECURITY64_H */
