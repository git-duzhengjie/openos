/*
 * security64.c — M6.4: 安全加固 (security hardening)
 *
 * 只做 CPU 硬件安全特性的探测与启用, 全部保守/幂等:
 *   - M6.4.1 CPUID 探测 NX / SMEP / SMAP / UMIP
 *   - M6.4.3 SMEP: 置 CR4.bit20, 禁止 CPL0 执行 user=1 页 (阻止 ret2usr)
 *   - M6.4.4 SMAP: 置 CR4.bit21, 禁止 CPL0 读写 user=1 页 (需 STAC/CLAC 放行)
 *   - M6.4.5 栈保护 canary: 初始化 __stack_chk_guard
 *
 * 无对应能力的 CPU (如默认 qemu64) 会安全跳过, 不影响启动。
 * 实测时用 -cpu qemu64,+smep,+smap 打开硬件特性。
 */
#include "../include/security64.h"
#include "../include/vmm64.h"

/* 链接脚本导出的内核段边界 (higher-half 虚拟地址, 4K 对齐) */
extern char __kernel64_start[];
extern char __etext64[];
extern char __rodata64_start[];
extern char __data64_start[];
extern char __kernel64_end[];

/* 共享内核串口打印 */
extern void early_serial64_write(const char *text);
#define arch_x86_64_serial_write early_serial64_write

/* ------------------------------------------------------------------ */
/* 底层原语                                                            */
/* ------------------------------------------------------------------ */

static inline void cpuid_raw(uint32_t leaf, uint32_t subleaf,
                             uint32_t *a, uint32_t *b,
                             uint32_t *c, uint32_t *d) {
    __asm__ __volatile__("cpuid"
                         : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                         : "a"(leaf), "c"(subleaf));
}

static inline uint64_t read_cr4(void) {
    uint64_t v;
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4(uint64_t v) {
    __asm__ __volatile__("mov %0, %%cr4" :: "r"(v) : "memory");
}

static inline uint64_t read_tsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* 追加 16 进制打印 (0x前缀, 定长 16 位) */
static void serial_hex64(const char *label, uint64_t v) {
    static const char digits[] = "0123456789abcdef";
    char buf[3 + 16 + 2];
    int i = 0;
    buf[i++] = '0';
    buf[i++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        buf[i++] = digits[(v >> shift) & 0xF];
    }
    buf[i++] = '\n';
    buf[i]   = '\0';
    arch_x86_64_serial_write(label);
    arch_x86_64_serial_write(buf);
}

/* ------------------------------------------------------------------ */
/* 探测                                                                */
/* ------------------------------------------------------------------ */

static openos_x86_64_security_caps_t g_caps;
static int g_probed = 0;

const openos_x86_64_security_caps_t *arch_x86_64_security_probe(void) {
    if (g_probed) {
        return &g_caps;
    }

    uint32_t a, b, c, d;

    /* NX/XD: CPUID 0x80000001, EDX bit20 */
    cpuid_raw(0x80000000u, 0, &a, &b, &c, &d);
    if (a >= 0x80000001u) {
        cpuid_raw(0x80000001u, 0, &a, &b, &c, &d);
        g_caps.nx = (d & (1u << 20)) != 0;
    }

    /* SMEP/SMAP/UMIP: CPUID leaf 7, subleaf 0 */
    cpuid_raw(0u, 0, &a, &b, &c, &d);
    if (a >= 7u) {
        cpuid_raw(7u, 0, &a, &b, &c, &d);
        g_caps.smep = (b & (1u << 7))  != 0;  /* EBX bit7  */
        g_caps.smap = (b & (1u << 20)) != 0;  /* EBX bit20 */
        g_caps.umip = (c & (1u << 2))  != 0;  /* ECX bit2  */
    }

    g_probed = 1;

    arch_x86_64_serial_write("[sec] CPU security caps: ");
    arch_x86_64_serial_write(g_caps.nx   ? "NX "   : "nx- ");
    arch_x86_64_serial_write(g_caps.smep ? "SMEP " : "smep- ");
    arch_x86_64_serial_write(g_caps.smap ? "SMAP " : "smap- ");
    arch_x86_64_serial_write(g_caps.umip ? "UMIP\n" : "umip-\n");

    return &g_caps;
}

const openos_x86_64_security_caps_t *arch_x86_64_security_caps(void) {
    return g_probed ? &g_caps : arch_x86_64_security_probe();
}

/* ------------------------------------------------------------------ */
/* M6.4.3 启用 SMEP                                                   */
/* ------------------------------------------------------------------ */

void arch_x86_64_security_enable_smep(void) {
    (void)arch_x86_64_security_probe();

    if (!g_caps.smep) {
        arch_x86_64_serial_write("[sec] SMEP unsupported by CPU, skip\n");
        return;
    }

    uint64_t before = read_cr4();
    write_cr4(before | OPENOS_X86_64_CR4_SMEP);
    uint64_t after = read_cr4();
    g_caps.smep_enabled = (after & OPENOS_X86_64_CR4_SMEP) != 0;

    serial_hex64("[sec] CR4 before SMEP = ", before);
    serial_hex64("[sec] CR4 after  SMEP = ", after);
    arch_x86_64_serial_write(g_caps.smep_enabled
        ? "[sec] M6.4.3 SMEP=on (kernel cannot exec user pages)\n"
        : "[sec] M6.4.3 SMEP enable FAILED\n");
}

/* ------------------------------------------------------------------ */
/* M6.4.4 启用 SMAP                                                   */
/* ------------------------------------------------------------------ */

void arch_x86_64_security_enable_smap(void) {
    (void)arch_x86_64_security_probe();

    if (!g_caps.smap) {
        arch_x86_64_serial_write("[sec] SMAP unsupported by CPU, skip\n");
        return;
    }

    uint64_t before = read_cr4();
    write_cr4(before | OPENOS_X86_64_CR4_SMAP);
    uint64_t after = read_cr4();
    g_caps.smap_enabled = (after & OPENOS_X86_64_CR4_SMAP) != 0;

    /* 启用后内核默认应 AC=0 (禁止越权访问 user 页)。 */
    if (g_caps.smap_enabled) {
        arch_x86_64_clac();
    }

    serial_hex64("[sec] CR4 before SMAP = ", before);
    serial_hex64("[sec] CR4 after  SMAP = ", after);
    arch_x86_64_serial_write(g_caps.smap_enabled
        ? "[sec] M6.4.4 SMAP=on (kernel user-mem access gated by STAC/CLAC)\n"
        : "[sec] M6.4.4 SMAP enable FAILED\n");
}

/* ------------------------------------------------------------------ */
/* M6.4.2 W^X 强制                                                       */
/* ------------------------------------------------------------------ */

#define OPENOS_X86_64_MSR_EFER   0xC0000080u
#define OPENOS_X86_64_EFER_NXE    (1ULL << 11)

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v) {
    __asm__ __volatile__("wrmsr" :: "c"(msr), "a"((uint32_t)v),
                         "d"((uint32_t)(v >> 32)));
}

/* 保证 EFER.NXE=1 (否则置 PTE.NX 位会 #GP)。需 CPU 支持 NX。 */
static int ensure_nxe(void) {
    (void)arch_x86_64_security_probe();
    if (!g_caps.nx) {
        arch_x86_64_serial_write("[sec] NX unsupported, cannot enforce W^X NX bits\n");
        return 0;
    }
    uint64_t efer = rdmsr(OPENOS_X86_64_MSR_EFER);
    if ((efer & OPENOS_X86_64_EFER_NXE) == 0) {
        wrmsr(OPENOS_X86_64_MSR_EFER, efer | OPENOS_X86_64_EFER_NXE);
        serial_hex64("[sec] EFER was = ", efer);
        serial_hex64("[sec] EFER now = ", rdmsr(OPENOS_X86_64_MSR_EFER));
    } else {
        arch_x86_64_serial_write("[sec] EFER.NXE already set by firmware\n");
    }
    return 1;
}

/* 对 [start,end) 区间逐页施加 set/clear 保护位。 */
static uint64_t protect_range(uint64_t start, uint64_t end,
                              uint64_t set_bits, uint64_t clear_bits) {
    uint64_t page = start & ~0xFFFULL;
    uint64_t count = 0;
    for (; page < end; page += 0x1000ULL) {
        if (arch_x86_64_vmm_protect_page(page, set_bits, clear_bits) == 0) {
            count++;
        }
    }
    return count;
}

/* W^X: 内核 .text = RX (清 RW), .rodata/.data/.bss = NX (保留 RW/RO)。
 * 结果: 任何页不会同时 "可写 + 可执行", 阻断代码注入。 */
void arch_x86_64_security_enforce_wxorx(void) {
    if (!ensure_nxe()) {
        arch_x86_64_serial_write("[sec] M6.4.2 W^X skipped (no NX)\n");
        return;
    }

    uint64_t text_s   = (uint64_t)__kernel64_start;
    uint64_t text_e   = (uint64_t)__etext64;
    uint64_t rodata_s = (uint64_t)__rodata64_start;
    uint64_t data_s   = (uint64_t)__data64_start;
    uint64_t kern_e   = (uint64_t)__kernel64_end;

    serial_hex64("[sec] .text   start = ", text_s);
    serial_hex64("[sec] .text   end   = ", text_e);
    serial_hex64("[sec] .rodata start = ", rodata_s);
    serial_hex64("[sec] .data   start = ", data_s);
    serial_hex64("[sec] kernel  end   = ", kern_e);

    /* .text: 清 RW -> RX (不可写); 不置 NX (需可执行)。 */
    uint64_t rx = protect_range(text_s, text_e, 0, OPENOS_X86_64_PTE_RW);
    /* .rodata: 清 RW + 置 NX -> 只读不可执行。 */
    uint64_t ro = protect_range(rodata_s, data_s,
                                OPENOS_X86_64_PTE_NX, OPENOS_X86_64_PTE_RW);
    /* .data/.bss: 置 NX (保留 RW) -> 可读写不可执行。 */
    uint64_t nx = protect_range(data_s, kern_e, OPENOS_X86_64_PTE_NX, 0);

    serial_hex64("[sec] .text   RX pages = ", rx);
    serial_hex64("[sec] .rodata RO pages = ", ro);
    serial_hex64("[sec] .data   NX pages = ", nx);
    arch_x86_64_serial_write("[sec] M6.4.2 W^X enforced (text=RX, rodata=RO-NX, data=RW-NX)\n");
}

/* ------------------------------------------------------------------ */
/* M6.4.5 栈保护 canary                                                */
/* ------------------------------------------------------------------ */

/* GCC -fstack-protector 会引用该全局符号做 canary 比较。 */
uint64_t __stack_chk_guard = 0x0;

void arch_x86_64_stack_guard_init(void) {
    /* 用 TSC 熵混合出一个非平凡 canary。低字节强制为 0 (NUL 终止),
     * 抵御 strcpy 类溢出改写 canary 高位。 */
    uint64_t t1 = read_tsc();
    uint64_t t2 = read_tsc();
    uint64_t g = (t1 * 0x9E3779B97F4A7C15ULL) ^ (t2 << 17) ^ (t2 >> 7);
    g ^= 0xC0FFEE00DEADBEEFULL;
    g &= ~0xFFULL;            /* 低字节清零 */
    if (g == 0) {            /* 极端退化保护 */
        g = 0xDEADBEEFCAFE0000ULL;
    }
    __stack_chk_guard = g;
    serial_hex64("[sec] stack canary = ", __stack_chk_guard);
}

/* GCC 在 canary 校验失败时调用。此处直接停机, 不返回。 */
void __stack_chk_fail(void) {
    arch_x86_64_serial_write("[sec] *** STACK SMASHING DETECTED — halting ***\n");
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}
