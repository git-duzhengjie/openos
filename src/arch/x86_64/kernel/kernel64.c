#include "../include/arch64.h"
#include "../include/compat32.h"
#include "../include/early_console64.h"
#include "../include/elf64_loader.h"
#include "../include/fdtable64.h"
#include "../include/percpu64.h"
#include "../include/gdt64.h"
#include "../include/heap64.h"
#include "../include/handoff64.h"
#include "../include/idt64.h"
#include "../include/idt_selftest64.h"
#include "../include/initrd64.h"
#include "../include/pic64.h"
#include "../include/pit64.h"
#include "../include/pmm64.h"
#include "../include/proc64.h"
#include "../include/sched64.h"
#include "../include/shell64.h"
#include "../include/syscall64.h"
#include "../include/syscall_selftest64.h"
#include "../include/sched_selftest64.h"
#include "../include/net64.h"
#include "../include/net_selftest64.h"
#include "../include/tsc64.h"
#include "../include/tsc_selftest64.h"
#include "../include/irq_selftest64.h"
#include "../include/sched_preempt_selftest64.h"
#include "../include/apic_selftest64.h"
#include "../include/acpi_selftest64.h"
#include "../include/smp_selftest64.h"
#include "../include/lapic64.h"  /* G.7g-1: lapic_timer_calibrate */
#include "../include/sched_prio_selftest64.h"

/* Step F.2: IRQ0 ISR entry implemented in isr64.S. Declared here so the
 * boot path can hand its address to arch_x86_64_idt_register_irq(). */
extern void x86_64_irq0(void);
extern void x86_64_irq_lapic_timer(void);  /* G.6.5a: AP per-CPU LAPIC timer */
extern void x86_64_irq_lapic_resched(void); /* G.6.6a: cross-CPU reschedule IPI */
#include "../include/tss64.h"
#include "../include/usermode64.h"
#include "../include/vfs64.h"
#include "../include/vmm64.h"
#include "arch_ops.h"
#include "platform_ops.h"
#include "x86_64_arch_ops.h"
#include "pc_uefi_platform_ops.h"
#include "basic_devices.h"

static const char x86_64_boot_log[] = "[x86_64] OpenOS entered kernel_main64\n";
static const char x86_64_console_log[] = "[x86_64] early console: serial + VGA ready\n";
static const char x86_64_pmm_log[] = "[x86_64] physical memory manager ready\n";
static const char x86_64_vmm_log[] = "[x86_64] 4-level virtual memory manager ready\n";
static const char x86_64_heap_log[] = "[x86_64] kernel heap allocator ready\n";
static const char x86_64_elf64_log[] = "[x86_64] ELF64 loader ready\n";
static const char x86_64_usermode_log[] = "[x86_64] usermode iretq return ready\n";
static const char x86_64_initrd_log[] = "[x86_64] initrd/VFS/shell bootstrap ready\n";
static const char x86_64_compat32_log[] = "[x86_64] compat32 evaluation ready\n";

void arch_x86_64_early_init(const openos_bootinfo_t *bootinfo) {
    openos_x86_64_arch_ops_init();
    openos_pc_uefi_platform_ops_init();
    arch_x86_64_tss_init();
    arch_x86_64_gdt_init();
    arch_x86_64_tss_load();
    /* G.6.2: install per-CPU "current" pointer for the BSP (cpu_idx=0).
     * Must come after gdt_init/tss_load (those reload %gs from a GDT data
     * descriptor with base=0 and would otherwise clobber the hidden base).
     * From this point onward, %gs:0 yields &g_percpu[0]. */
    arch_x86_64_percpu_install_gs(0);
    arch_x86_64_idt_init();
    early_console64_init();
    early_console64_write("[x86_64] arch_ops=");
    early_console64_write(openos_arch_ops_name());
    early_console64_write(" platform_ops=");
    early_console64_write(openos_platform_ops_name());
    early_console64_write("\n");
    openos_basic_devices_register();
    early_console64_write("[x86_64] device_model initialized\n");

    if (openos_bootinfo_is_valid(bootinfo) &&
        (bootinfo->flags & OPENOS_BOOTINFO_FLAG_FRAMEBUFFER_VALID)) {
        early_framebuffer64_info_t framebuffer;
        framebuffer.base = bootinfo->framebuffer.base;
        framebuffer.width = bootinfo->framebuffer.width;
        framebuffer.height = bootinfo->framebuffer.height;
        framebuffer.pitch = bootinfo->framebuffer.pitch;
        framebuffer.bpp = bootinfo->framebuffer.bpp;
        framebuffer.format = OPENOS_X86_64_EARLY_FB_FORMAT_XRGB8888;
        framebuffer.available = 1u;
        early_framebuffer64_init(&framebuffer);
        early_console64_write("[x86_64] OpenOSBootInfo framebuffer active\n");
    } else {
        early_console64_write("[x86_64] OpenOSBootInfo framebuffer missing\n");
    }

    arch_x86_64_sched_init();
    arch_x86_64_syscall_init();
    arch_x86_64_memory_init_from_bootinfo(bootinfo);
    arch_x86_64_vmm_init();
    arch_x86_64_heap_init();
    arch_x86_64_elf64_loader_init();
    arch_x86_64_usermode_init();
    arch_x86_64_initrd_init(bootinfo);
    arch_x86_64_vfs_init();
    arch_x86_64_fd_init();
    arch_x86_64_shell_init();
    arch_x86_64_compat32_init();
    /* Step E.1: bring up the minimal PCB pool. Slot 0 = kernel proc (pid=1). */
    arch_x86_64_proc_init();
    /* Step E.3: loopback socket layer. Allocation-free, lives in .bss so it
     * is safe to bring up alongside fd_init / vfs_init in early boot. */
    arch_x86_64_net_init();
    /* Step E.4: TSC<->PIT calibration. Must run before any selftest that
     * relies on uptime_ms(); idempotent and tolerates failure (uptime falls
     * back to the legacy rdtsc>>20 estimate). */
    arch_x86_64_tsc_init();
    /* Step F.2: 8259A remap (master 0x20, slave 0x28) + PIT channel 0
     * @ 100 Hz. IRQs stay globally disabled here — the IRQ self-test will
     * be the first code to flip the IF bit, and it cleans up after itself.
     * Order matters: remap before any sti, otherwise BIOS-default IRQ0
     * would fire on the #DF vector and panic. */
    arch_x86_64_pic_init();
    arch_x86_64_pit_init(OPENOS_X86_64_PIT_HZ_DEFAULT);
    arch_x86_64_idt_register_irq(0x20u, x86_64_irq0);

    /* G.6.5a: register the AP per-CPU LAPIC-timer ISR on vector 0x40.
     * Registration lives on the BSP side because the IDT is shared
     * structurally across cores (each CPU loads the same IDT base via
     * arch_x86_64_idt_load_ap). By installing the gate here — strictly
     * before arch_x86_64_smp_init() fires the AP bring-up sequence — we
     * guarantee that the first AP-side LAPIC timer interrupt finds a
     * fully wired handler. The BSP itself never programs its LAPIC timer
     * in G.6.5a, so it will not enter this ISR. */
    arch_x86_64_idt_register_irq(0x40u, x86_64_irq_lapic_timer);

    /* G.6.6a: register the cross-CPU reschedule-IPI handler on vector
     * 0x41. Same ordering contract as 0x40 — the gate must be live on
     * every CPU before arch_x86_64_smp_init() returns, otherwise the
     * first IPI BSP sends to an AP during selftest would land on an
     * unwired vector and #GP. Vector 0x41 avoids the legacy PIC range
     * (0x20–0x2F), the timer at 0x40, and the spurious vector 0xFF. */
    arch_x86_64_idt_register_irq(0x41u, x86_64_irq_lapic_resched);
}

void kernel_main64_with_handoff(const uefi64_handoff_info_t *handoff) {
    const openos_bootinfo_t *bootinfo = arch_x86_64_bootinfo_from_uefi_handoff(handoff);

    arch_x86_64_early_init(bootinfo);
    early_console64_write(x86_64_boot_log);
    if (openos_bootinfo_is_valid(bootinfo) &&
        (bootinfo->flags & OPENOS_BOOTINFO_FLAG_CMDLINE_VALID)) {
        early_console64_write("[x86_64] OpenOSBootInfo cmdline base=");
        early_console64_write_hex64(bootinfo->cmdline);
        early_console64_write(" size=");
        early_console64_write_hex64(bootinfo->cmdline_size);
        early_console64_write("\n");
    }
    arch_x86_64_gdt_print_status();
    arch_x86_64_tss_print_status();
    arch_x86_64_idt_print_status();
    arch_x86_64_handoff_print_status();
    early_console64_write(x86_64_console_log);
    arch_x86_64_sched_print_status();
    arch_x86_64_syscall_print_status();
    early_console64_write(x86_64_pmm_log);
    arch_x86_64_pmm_print_status();
    early_console64_write(x86_64_vmm_log);
    arch_x86_64_vmm_print_status();
    early_console64_write(x86_64_heap_log);
    arch_x86_64_heap_print_status();
    early_console64_write(x86_64_elf64_log);
    arch_x86_64_elf64_loader_print_status();
    early_console64_write(x86_64_usermode_log);

    early_console64_write(x86_64_initrd_log);
    int initrd_mount_status = arch_x86_64_vfs_mount_initrd(arch_x86_64_initrd_get_image());
    early_console64_write("[x86_64][initrd] mount_status=");
    early_console64_write_hex64((uint64_t)(uint32_t)initrd_mount_status);
    early_console64_write("\n");
    arch_x86_64_initrd_print_status();
    arch_x86_64_vfs_print_status();

    /* Step F.1 IDT registration selftest — runs as the very first selftest
     * because every later subsystem (syscall, sched, net, tsc, ring3 drop)
     * needs the IDT to route #PF/#GP/#UD/etc. to our C handlers. A
     * silently-broken gate here would otherwise turn a follow-on bug
     * into a triple-fault reset with zero log. The complementary
     * post-hello64 kernel-fault sentry (Step G.x) verifies after the
     * fact that no ring0 exception slipped through during the ring3
     * round-trip. */
    {
        int idt_rv = arch_x86_64_idt_selftest_run();
        early_console64_write("[x86_64][idt-selftest] result=");
        early_console64_write_hex64((uint64_t)(uint32_t)idt_rv);
        early_console64_write("\n");
    }

    /* Step C kernel-side selftest — 不依赖 OVMF/ring3 跳转，直接验证 dispatch→VFS→initrd 链路。 */
    {
        int selftest_rv = arch_x86_64_syscall_selftest_run();
        early_console64_write("[x86_64][selftest] result=");
        early_console64_write_hex64((uint64_t)(uint32_t)selftest_rv);
        early_console64_write("\n");
    }

    /* Step E.2 cooperative scheduler selftest — must run before we drop
     * into ring3, otherwise the bootstrap context's stack starts being
     * shared with the user-mode trampoline path. */
    {
        int sched_rv = arch_x86_64_sched_selftest_run();
        early_console64_write("[x86_64][sched-selftest] result=");
        early_console64_write_hex64((uint64_t)(uint32_t)sched_rv);
        early_console64_write("\n");
        arch_x86_64_sched_print_status();
    }

    /* Step E.3 loopback socket selftest — still kernel-side, so any future
     * regression in the socket API surfaces before ring3 work begins. */
    {
        int net_rv = arch_x86_64_net_selftest_run();
        early_console64_write("[x86_64][net-selftest] result=");
        early_console64_write_hex64((uint64_t)(uint32_t)net_rv);
        early_console64_write("\n");
        arch_x86_64_net_print_status();
    }

    /* Step E.4 TSC<->PIT calibration sanity. Non-fatal — uptime_ms() falls
     * back to the legacy estimator if anything in this chain goes sideways. */
    arch_x86_64_tsc_selftest_run();

    /* Step F.2: IRQ0 chain self-test. Briefly enables interrupts, expects
     * ~20 PIT ticks over 200 ms, then disables IRQs again. Must follow
     * tsc_selftest_run so per_ms is known-good. */
    arch_x86_64_irq_selftest_run();

    /* Step F.3: preemptive scheduler self-test. Spawns two non-yielding
     * spin kthreads and lets IRQ0 (PIT @100Hz) drive context switches
     * via arch_x86_64_sched_on_tick(). Must follow irq_selftest_run so
     * PIC remap + PIT chain are proven good. The test re-masks IRQ0 on
     * exit so the downstream ring3 hello64 path inherits IRQs-off as
     * before. */
    (void)arch_x86_64_sched_preempt_selftest_run();

    /* Step G.3a: parse ACPI tables (RSDP via EFI cfg table -> XSDT -> MADT)
     * to enumerate CPUs and IO-APICs. Must run BEFORE apic_selftest so the
     * latter can later be evolved to use MADT-discovered LAPIC/IOAPIC bases
     * instead of the hard-coded MMIO addresses. Failure is non-fatal: the
     * G.1/G.2 path falls back to its compile-time defaults. */
    (void)arch_x86_64_acpi_selftest_run();

    /* Step G.1: switch IRQ0 routing from the 8259A to LAPIC/IOAPIC. From
     * this point on, lapic_is_ready() flips true and pit's IRQ0 handler
     * EOIs via LAPIC. Failure is non-fatal: PIC remains masked-on the
     * way it was after F.2 / F.3, so the system keeps booting on the
     * legacy path. */
    (void)arch_x86_64_apic_selftest_run();

    /* G.7g-1: LAPIC bus-frequency calibration. Must run AFTER apic_selftest
     * (LAPIC mapped + SVR enabled) and BEFORE smp_selftest (which wakes APs
     * that will read g_lapic_bus_ticks_per_ms). TSC was already calibrated
     * at Step E.4 above, so we have a stable wall-clock reference. */
    {
        bool cal_ok = arch_x86_64_lapic_timer_calibrate();
        uint32_t cal_tpm = arch_x86_64_lapic_timer_ticks_per_ms();
        early_console64_write("[x86_64][lapic-cal] ok=");
        early_console64_write_hex64((uint64_t)(cal_ok ? 1u : 0u));
        early_console64_write(" ticks_per_ms=");
        early_console64_write_hex64((uint64_t)cal_tpm);
        early_console64_write("\n");
    }

    /* Step G.4.1: SMP topology snapshot (no AP wakeup yet). Must run AFTER
     * apic_selftest so LAPIC is initialized and BSP apic_id is readable. */
    arch_x86_64_smp_selftest_run();

    /* Step G.2: priority-weighted scheduling self-test. Spawns three
     * spin kthreads at HIGH / NORMAL / LOW priorities and asserts the
     * canonical CPU-share ordering H > N > L. Runs through the
     * IOAPIC GSI2 path established by G.1 (falls back to PIC if LAPIC
     * isn't ready). Re-masks IRQ0 on exit so downstream ring3 path
     * keeps inheriting "IRQs-off" precondition. */
    (void)arch_x86_64_sched_prio_selftest_run();

    /* Step C: initrd & fdtable 就绪后再跳 ring3 hello64，否则 open(/hello.txt) 会看不到文件。
     * H.2: image 不再直连 embed_hello64 数组，改走 initrd 通路。kernel64.c
     * 不再 #include embed_hello64.h；image 真正的 single source of truth
     * 是 initrd 路径 /bin/hello64。将来换成 boot-loaded initrd 时，只动
     * initrd64.c 的 file table，无需再碰 kernel64.c。 */
    const x86_64_initrd_file_t *hello64_file = arch_x86_64_initrd_find("/bin/hello64");
    elf64_load_result_t hello64;
    if (hello64_file == NULL) {
        early_console64_write("[x86_64][user] /bin/hello64 not found in initrd\n");
        hello64.status = ELF64_LOADER_ERR_BAD_ARGUMENT;
        hello64.entry = 0u;
        hello64.low_addr = 0u;
        hello64.high_addr = 0u;
        hello64.brk_start = 0u;
        hello64.load_segments = 0u;
    } else {
        early_console64_write("[x86_64][user] loading /bin/hello64 from initrd size=");
        early_console64_write_hex64((uint64_t)hello64_file->size);
        early_console64_write("\n");
        hello64 = arch_x86_64_elf64_load_image(hello64_file->data, hello64_file->size);
    }
    if (hello64.status == ELF64_LOADER_OK) {
        /* Step E.1: register the ring3 program as a real PCB so SYS_GETPID
         * returns the spawned pid (=2) instead of the old hard-coded 1. */
        uint32_t hello_pid = arch_x86_64_proc_spawn_user("hello64");
        early_console64_write("[x86_64][proc] spawned hello64 pid=");
        early_console64_write_hex64((uint64_t)hello_pid);
        early_console64_write("\n");
        early_console64_write("[x86_64][user] entering ring3 hello64 entry=");
        early_console64_write_hex64((uint64_t)hello64.entry);
        early_console64_write("\n");
        (void)arch_x86_64_usermode_run(hello64.entry);
        early_console64_write("[x86_64][user] hello64 returned exit_code=");
        early_console64_write_hex64((uint64_t)(uint32_t)arch_x86_64_usermode_exit_code());
        early_console64_write(" exited=");
        early_console64_write_hex64(arch_x86_64_usermode_has_exited());
        early_console64_write("\n");
    } else {
        early_console64_write("[x86_64][user] hello64 load failed status=");
        early_console64_write_hex64((uint64_t)(uint32_t)hello64.status);
        early_console64_write("\n");
    }
    arch_x86_64_elf64_loader_print_status();
    arch_x86_64_usermode_print_status();
    arch_x86_64_proc_print_status();

    /* Step G.x post-EXIT kernel-fault sentry selftest.
     *
     * After the ring3 hello64 round-trip completes, we expect:
     *   - canary == 2 (return path executed end-to-end)
     *   - kfault_delta == 0 (no #UD/#GP/#PF/etc. in ring0 during the
     *     entire round-trip; SYS_EXIT goes through the syscall path,
     *     not the IDT, so this must stay flat)
     *
     * If either fails, the original 0b14358-class bug has come back —
     * dump the IDT sentry snapshot so the regression points straight
     * at the offending RIP. */
    {
        uint64_t canary = arch_x86_64_usermode_canary();
        uint64_t kfdelta = arch_x86_64_usermode_kfault_delta();
        early_console64_write("[x86_64][post-exit-sentry] canary=");
        early_console64_write_hex64(canary);
        early_console64_write(" kfault_delta=");
        early_console64_write_hex64(kfdelta);
        if (canary == 2 && kfdelta == 0) {
            early_console64_write(" result=PASS\n");
        } else {
            early_console64_write(" result=FAIL\n");
            arch_x86_64_idt_print_kernel_fault_stats();
        }
    }

    arch_x86_64_shell_run_init();
    arch_x86_64_shell_print_status();
    arch_x86_64_vfs_print_status();
    early_console64_write(x86_64_compat32_log);
    arch_x86_64_compat32_print_status();
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void kernel_main64(void) {
    kernel_main64_with_handoff(0);
}
