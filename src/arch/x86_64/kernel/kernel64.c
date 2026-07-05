#include "../include/arch64.h"
#include "../include/address_space64.h"
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
#include "../include/ramfs64.h"
#include "../include/ata64.h"
#include "../include/fat32_64.h"
#include "pci.h"
#include "virtio_net.h"
/* M1.3 网络协议栈入口（netstack.c） */
extern void net_init(void);
extern void net_tick(uint32_t elapsed_ms);
extern int net_ping_ipv4(uint32_t dst_ip);
extern void net_print_info(void);
#include "../include/pic64.h"
#include "../include/pit64.h"
#include "../include/pmm64.h"
#include "../include/proc64.h"
#include "../include/sched64.h"
#include "../include/shell64.h"
#include "../include/syscall64.h"
#include "../include/syscall_selftest64.h"
#include "../include/as_selftest64.h"
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

/* GUI 移植 (范围 B): i386 图形桌面已链入 x86_64 内核。
 * window_manager_start_desktop() 初始化 GUI 子系统并绘制桌面，
 * window_manager_poll() 处理一帧（鼠标/重绘）。这些符号来自
 * src/kernel/window_manager.c，声明在 include/window_manager.h。 */
extern int  window_manager_start_desktop(void);
extern void window_manager_poll(void);
extern void framebuffer_init(void);  /* UEFI GOP 后端初始化 (framebuffer64.c) */
extern int  arch_x86_64_mouse_install(void);  /* PS/2 鼠标接入 (mouse64.c) */
extern int  arch_x86_64_keyboard_install(void);  /* PS/2 键盘接入 (keyboard64.c) */
extern void gui_invalidate_all(void);  /* 标记整屏为脏，下一帧全屏重绘 */
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
    /* Step M1.1: PCI 总线枚举。扫描所有总线/设备/功能，解析 BAR 与 IRQ，
     * 为后续网卡 / AHCI / NVMe / xHCI / 声卡驱动提供设备发现基础。
     * 仅做只读枚举 + 日志，无中断依赖，安全早期执行。 */
    pci_scan_all();
    pci_dump_devices();
    /* Step M1.2: virtio-net 网卡驱动。基于上面的 PCI 枚举查找 1af4:1000，
     * 建立 legacy split virtqueue，读取 MAC，投递 RX 缓冲，进入 DRIVER_OK。
     * 之后即可通过 virtio_net_send / virtio_net_poll_recv 收发以太网帧。 */
    virtio_net_init();
    virtio_net_dump();
    /* Step M1.3: 真实网络协议栈（Ethernet + ARP + IPv4 + ICMP + UDP）。
     * 挂载到 virtio-net 之上，注册 eth0，配置静态 IP(10.0.2.15)，
     * 之后可响应 ARP、被 ping、主动 ping、收发 UDP。 */
    net_init();
    /* M1.5.1 自检：DHCP 动态获取 IP。发 DISCOVER，轮询等待 OFFER/ACK。
     * QEMU user 网络内置 DHCP 服务器(10.0.2.2)，正常会分配 10.0.2.15。 */
    {
        extern void early_serial64_write(const char *s);
        extern int net_dhcp_start(void);
        extern int net_dhcp_state(void);
        extern void net_tick(uint32_t);
        early_serial64_write("[net] 自检：DHCP 动态获取 IP ...\n");
        net_dhcp_start();
        /* 轮询驱动收包，最多等待若干轮 */
        for (int i = 0; i < 200000 && net_dhcp_state() != 3; i++) {
            net_tick(0);
        }
        early_serial64_write(net_dhcp_state() == 3
            ? "[net] DHCP PASS: 已动态获取 IP\n"
            : "[net] DHCP 未完成(回退静态或稍后重试)\n");
    }
    /* M1.3 自检：主动 ping 网关 10.0.2.2，验证 ARP+ICMP 全链路 */
    {
        extern void early_serial64_write(const char *s);
        uint32_t gw = (10u<<24)|(0u<<16)|(2u<<8)|2u;
        early_serial64_write("[net] 自检：ping 10.0.2.2 ...\n");
        int pr = net_ping_ipv4(gw);
        early_serial64_write(pr == 0 ? "[net] PING PASS: 网关可达\n"
                                     : "[net] PING TIMEOUT: 无应答(QEMU user模式下属正常)\n");
        net_print_info();
    }
    /* M1.4 自检：TCP 主动三次握手。向 10.0.2.2:8888 发 SYN，等待 SYN-ACK，
     * 回 ACK 后进入 ESTABLISHED。QEMU user 网络下 10.0.2.2 代理到 host
     * loopback，需 host 侧监听 8888 (见 build+run.bat 前置的 python server)。
     * 若无服务器应答则停在 SYN_SENT，属正常，不判失败。 */
    {
        extern void early_serial64_write(const char *s);
        extern int net_tcp_open(uint32_t,uint16_t,uint32_t,uint16_t,int);
        extern int net_tcp_state(int);
        extern void net_tick(uint32_t);
        uint32_t gw = (10u<<24)|(0u<<16)|(2u<<8)|100u; /* SLIRP guestfwd 目标地址 */
        early_serial64_write("[net] 自检：TCP connect 10.0.2.100:8888 (三次握手) ...\n");
        int cid = net_tcp_open(0, 45000, gw, 8888, 1);
        if (cid < 0) {
            early_serial64_write("[net] TCP open 失败\n");
        } else {
            for (int i = 0; i < 200000 && net_tcp_state(cid) != 4; i++) {
                net_tick(0);
            }
            early_serial64_write(net_tcp_state(cid) == 4
                ? "[net] TCP PASS: 三次握手完成，连接 ESTABLISHED\n"
                : "[net] TCP 握手未完成(无服务器应答属正常)\n");
        }
    }
    /* M1.5.2 自检：DNS 解析 A 记录(向 DHCP 下发的 DNS 服务器) */
    {
        extern void early_serial64_write(const char *s);
        extern int net_dns_resolve(const char *hostname, uint32_t *out_ip);
        uint32_t rip = 0;
        early_serial64_write("[net] 自检：DNS 解析 example.com ...\n");
        int dr = net_dns_resolve("example.com", &rip);
        early_serial64_write(dr == 0
            ? "[net] DNS PASS: 域名解析成功\n"
            : "[net] DNS 解析未完成(无网络应答属正常)\n");
    }
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

    /* 阶段二：探测 secondary IDE 上的持久化数据盘（ATA PIO / LBA28）。
     * 必须在 ramfs_init() 之前，以便后续从磁盘快照恢复文件树。 */
    if (ata_init()) {
        early_console64_write("[x86_64][ata] data disk ready\n");
    } else {
        early_console64_write("[x86_64][ata] no data disk (RAM-only mode)\n");
    }

    /* 阶段 4-1：探测 secondary slave 上的 FAT32 数据盘并挂载。
     * 与 master 持久化盘隔离，可与 Windows/U盘交换文件。 */
    if (ata_slave_init()) {
        early_console64_write("[x86_64][ata] FAT32 disk detected (slave)\n");
        /* part_lba 传 0：自动探测 MBR 分区，无分区则整盘 FAT32 */
        if (fat32_mount(ata_slave_read_sectors, 0) == 0) {
            early_console64_write("[x86_64][fat32] mounted at /mnt/fat\n");
            /* 阶段 4-3：使能写入（注入 slave 写扇区回调） */
            fat32_set_write_fn(ata_slave_write_sectors);
            if (fat32_writable())
                early_console64_write("[x86_64][fat32] write enabled (RW)\n");
            fat32_selftest();
        } else {
            early_console64_write("[x86_64][fat32] mount failed\n");
        }
    } else {
        early_console64_write("[x86_64][ata] no FAT32 disk\n");
    }

    /* 阶段一：初始化 RAMFS 内存树文件系统（将 initrd 导入为可读写树）。
     * 必须在 initrd 就绪之后调用；GUI 终端/文件浏览器的 vfs_* 均走此树。 */
    ramfs_init();
    early_console64_write("[x86_64][ramfs] tree ready\n");

    /* 阶段二：若数据盘上存在有效快照，则加载覆盖内存树（持久化恢复）。 */
    {
        int lr = ramfs_snapshot_load();
        if (lr == 0) {
            early_console64_write("[x86_64][ramfs] snapshot restored from disk\n");
        } else if (lr == 1) {
            early_console64_write("[x86_64][ramfs] no snapshot on disk (fresh tree)\n");
            /* 首次启动: 将 initrd 初始树落盘为初始快照, 使后续启动可持久化恢复 */
            if (ramfs_snapshot_save() == 0) {
                early_console64_write("[x86_64][ramfs] initial snapshot written to disk\n");
            } else {
                early_console64_write("[x86_64][ramfs] initial snapshot save skipped/failed\n");
            }
        } else {
            early_console64_write("[x86_64][ramfs] snapshot load failed (fresh tree)\n");
        }
    }

    /* 阶段 4-2：验证 FAT32 已接入 VFS 挂载点 /mnt/fat（走 vfs_* int-fd 接口，
     * 与 GUI 终端 ls/cat 走同一条路径）。必须在 ramfs_init() 之后。 */
    if (fat32_mounted()) {
        early_console64_write("[x86_64][fat32-vfs] === /mnt/fat selftest ===\n");
        /* 1) readdir 根目录 */
        for (int i = 0; ; i++) {
            dentry_t *de = vfs_readdir("/mnt/fat", i);
            if (!de) break;
            early_console64_write("[x86_64][fat32-vfs] entry: ");
            early_console64_write(de->name);
            if (de->inode && (de->inode->mode & FS_DIR))
                early_console64_write("  <DIR>");
            early_console64_write("\n");
            if (i > 32) break;
        }
        /* 2) stat + open + read HELLO.TXT */
        {
            inode_t st;
            if (vfs_stat("/mnt/fat/HELLO.TXT", &st) == 0) {
                early_console64_write("[x86_64][fat32-vfs] stat HELLO.TXT size=");
                early_console64_write_hex64((uint64_t)(uint32_t)st.size);
                early_console64_write("\n");
            }
            int fd = vfs_open("/mnt/fat/HELLO.TXT", O_RDONLY, 0);
            if (fd >= 0) {
                char buf[128];
                int rn = vfs_read(fd, buf, sizeof(buf) - 1);
                if (rn > 0) {
                    buf[rn] = 0;
                    early_console64_write("[x86_64][fat32-vfs] read HELLO.TXT: ");
                    early_console64_write(buf);
                    early_console64_write("\n");
                }
                vfs_close(fd);
            } else {
                early_console64_write("[x86_64][fat32-vfs] open HELLO.TXT FAILED\n");
            }
        }
        early_console64_write("[x86_64][fat32-vfs] === selftest done ===\n");
    }

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

    /* H.5b.3.A AS deep-clone selftest — verifies fork() page-table
     * machinery (PML4[1] subtree dup, leaf 4KiB byte copy, isolation).
     * Runs entirely on the boot identity, no CR3 flip. */
    {
        int as_rv = arch_x86_64_as_selftest_clone_run();
        early_console64_write("[x86_64][as-selftest] result=");
        early_console64_write_hex64((uint64_t)(uint32_t)as_rv);
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

    /* ============================================================
     * GUI 桌面启动 —— 早期验证路径 (GUI_EARLY_VERIFY)
     *
     * 用户态 fork-multi 自检当前 wait 回收极慢（分钟级），会挡住
     * 主线到达文件末尾的 GUI 启动点。为先行验证"桌面能否显示"，
     * 这里在 ring3 测试之前拉起 GUI 桌面。验证通过后再决定最终
     * 落点（或修复 fork wait 后移回文件末尾）。
     * ============================================================ */
#ifdef SHELL_LAUNCH_SELFTEST
    /* Route-2 end-to-end probe: exercise the shell 'run' path (which in
     * turn drives arch_x86_64_usermode_launch_path + VFS-first ELF load)
     * before the GUI desktop takes over the poll loop. Serial-only. */
    early_console64_write("[x86_64][r2-selftest] shell 'run /bin/hello64' ...\n");
    arch_x86_64_shell_exec_line("run /bin/hello64");
    early_console64_write("[x86_64][r2-selftest] done\n");
#endif

#ifdef GUI_EARLY_VERIFY
    early_console64_write("[x86_64][gui] (early) starting desktop before ring3 test...\n");
    {
        framebuffer_init();
        int gui_rc = window_manager_start_desktop();
        early_console64_write("[x86_64][gui] window_manager_start_desktop rc=");
        early_console64_write_hex64((uint64_t)(uint32_t)gui_rc);
        early_console64_write("\n");
        if (gui_rc == 0) {
            /* 桌面已接管 framebuffer：先关闭 fbcon 屏幕输出，避免后续内核
             * 日志继续叠印在桌面底部（串口日志仍照常输出）。关闭后再打印的
             * 日志只走串口，不会画到屏幕上。 */
            early_console64_set_fbcon(0);
            early_console64_write("[x86_64][gui] desktop up, entering poll loop\n");
            /* 标记整屏为脏并立即重绘一帧，把关闭 fbcon 之前残留在屏幕
             * 最底部（桌面重绘范围之外）的内核日志文字彻底覆盖掉。 */
            gui_invalidate_all();
            window_manager_poll();
            /* 桂载 PS/2 鼠标：注册 IRQ12 网关 + IOAPIC 路由 + 设备使能。
             * desktop 已起来，gui_poll() 通过 mouse_snapshot_and_clear_delta()
             * 消费光标位置；这里把真实驱动接进中断系统。 */
            if (arch_x86_64_mouse_install() != 0) {
                early_console64_write("[x86_64][gui] WARN mouse install failed; desktop runs without pointer\n");
            }
            /* 挂载 PS/2 键盘：注册 IRQ1 网关 + IOAPIC 路由 + 控制器使能。
             * gui_post_key_code_with_modifiers() 把按键喂给桌面/窗口。 */
            if (arch_x86_64_keyboard_install() != 0) {
                early_console64_write("[x86_64][gui] WARN keyboard install failed; desktop runs without keys\n");
            }
            __asm__ __volatile__("sti");  /* 开中断，让 IRQ12 能进来 */

            /* 开机锁屏门闸：桌面已就绪、键鼠已 install、中断已开，
             * 此处阻塞等待用户输入正确密码（默认 openos）才放行进桌面。
             * lockscreen_run() 内部自建全屏密码窗口并驱动 window_manager_poll()。*/
            {
                extern void lockscreen_run(void);
                lockscreen_run();
            }
            for (;;) {
                window_manager_poll();
                __asm__ __volatile__("hlt");
            }
        }
        early_console64_write("[x86_64][gui] (early) desktop start FAILED, continue to ring3 test\n");
    }
#endif

    /* Step C: initrd & fdtable 就绪后再跳 ring3 hello64，否则 open(/hello.txt) 会看不到文件。
     * H.2: image 不再直连 embed_hello64 数组，改走 initrd 通路。kernel64.c
     * 不再 #include embed_hello64.h；image 真正的 single source of truth
     * 是 initrd 路径 /bin/hello64。将来换成 boot-loaded initrd 时，只动
     * initrd64.c 的 file table，无需再碰 kernel64.c。 */
    /* Step C: initrd & fdtable demo for ring3 hello64. open(/hello.txt)
     * reads the embedded text file.
     * H.2: ELF image source switched from compile-time embed_hello64.h to
     * the initrd path /bin/hello64. kernel64.c no longer #includes the
     * embed header directly -- single source of truth lives in initrd.
     * H.3: initial program is now /bin/launcher, which execve's into
     * /bin/hello64_v2 to exercise the SYS_EXEC trampoline. After ring3
     * returns we inspect usermode_has_pending_exec(): if set, we reload
     * the new image and re-enter ring3 with the same proc slot. Bounded
     * loop (max 4 rounds) to make a runaway execve chain panic-safe. */
    const char *initial_path = "/bin/launcher";
    const x86_64_initrd_file_t *initial_file = arch_x86_64_initrd_find(initial_path);
    elf64_load_result_t hello64;
    /* H.5b.2 step A: AS shared across the else-branch and the
     * post-load PCB-binding block. Declared NULL so the
     * not-found / load-failure paths fall through cleanly. */
    struct x86_64_address_space *initial_as = ((struct x86_64_address_space *)0);
    if (initial_file == NULL) {
        early_console64_write("[x86_64][user] initial program not found in initrd path=");
        early_console64_write(initial_path);
        early_console64_write("\n");
        hello64.status = ELF64_LOADER_ERR_BAD_ARGUMENT;
        hello64.entry = 0u;
        hello64.low_addr = 0u;
        hello64.high_addr = 0u;
        hello64.brk_start = 0u;
        hello64.load_segments = 0u;
    } else {
        early_console64_write("[x86_64][user] loading ");
        early_console64_write(initial_path);
        early_console64_write(" from initrd size=");
        early_console64_write_hex64((uint64_t)initial_file->size);
        early_console64_write("\n");
        /*
         * H.5b.2 step A: pre-create an address space for the initial
         * user image. We mirror every PT_LOAD into it via the new
         * arch_x86_64_elf64_load_image_into() helper so that step B can
         * promote CR3 into this AS without losing any user page. The
         * legacy boot-identity ring3 path stays live (phys==va writes
         * still happen), so this commit must be a strict zero-behavior
         * change for SMP=1/4 Stages 1-30.
         */
        initial_as = arch_x86_64_as_create();
        if (initial_as == ((struct x86_64_address_space *)0)) {
            early_console64_write("[x86_64][as] create FAILED -- falling back to boot identity\n");
        }
        hello64 = arch_x86_64_elf64_load_image_into(initial_file->data, initial_file->size, initial_as);
    }
    if (hello64.status == ELF64_LOADER_OK) {
        /* Step E.1: register the ring3 program as a real PCB so SYS_GETPID
         * returns the spawned pid (=2) instead of the old hard-coded 1.
         * H.3: pid is preserved across execve -- we spawn once. */
        uint32_t hello_pid = arch_x86_64_proc_spawn_user("launcher");
        /* H.5b.2 step A: bind the pre-created AS to the launcher PCB
         * now that current_index has switched to it. */
        if (initial_as != ((struct x86_64_address_space *)0)) {
            arch_x86_64_proc_current_set_as(initial_as);
        }
        early_console64_write("[x86_64][proc] spawned launcher pid=");
        early_console64_write_hex64((uint64_t)hello_pid);
        early_console64_write("\n");
        early_console64_write("[x86_64][user] entering ring3 entry=");
        early_console64_write_hex64((uint64_t)hello64.entry);
        early_console64_write("\n");
        x86_64_entry_t next_entry = hello64.entry;
        /*
         * H.4: seed argv for the initial program. We pass argv[0] =
         * "/bin/launcher" to mimic the convention that argv[0] is
         * always the program name as seen by the caller. /bin/launcher
         * will print this to confirm initial-spawn argv works the
         * same as execve-spawned argv (they share the same
         * arch_x86_64_usermode_seed_user_stack path).
         */
        {
            /*
             * H.4: argv[0] = "/bin/launcher" mimics SysV convention
             * that argv[0] is the program name as seen by the caller.
             * launcher will print this to confirm initial-spawn argv
             * works the same as execve-spawned argv (they share the
             * same arch_x86_64_usermode_seed_user_stack path).
             *
             * Note: this used to require a stack-local workaround
             * because a file-scope static .data array read back as
             * zero — root cause was the UEFI ELF loader merging
             * .data + .bss into one PT_LOAD whose huge p_memsz
             * forced AllocateAnyPages fallback, landing .data at
             * a phys address the boot page tables didn't cover.
             * Fixed by splitting .bss into its own PT_LOAD (filesz=0)
             * via explicit PHDRS in linker64.ld. See: commit after caf589d.
             */
            static const char *initial_argv[2] = {
                "/bin/launcher",
                (const char *)0,
            };
            arch_x86_64_usermode_set_args(1, initial_argv);

            /*
             * H.5a: seed an initial envp for the very first user image so
             * launcher (and anyone after it that forgets to override) sees
             * a non-empty environment. This proves the envp plumbing on
             * the *spawn* path mirrors the *execve* path.
             */
            static const char *initial_envp[3] = {
                "BOOT_STAGE=H.5a",
                "OPENOS_BOOT=uefi",
                (const char *)0,
            };
            arch_x86_64_usermode_set_envs(2, initial_envp);
        }
        const int kExecRoundCap = 4;
        int round = 0;
        int fork_rounds = 0;
        const int kForkRoundCap = 2; /* alpha: at most one child */
        for (;;) {
            (void)arch_x86_64_usermode_run(next_entry);
            if (arch_x86_64_usermode_has_pending_exec()) {
                next_entry = arch_x86_64_usermode_take_pending_exec();
                ++round;
                early_console64_write("[x86_64][usermode] exec round=");
                early_console64_write_hex64((uint64_t)(uint32_t)round);
                early_console64_write(" next_entry=");
                early_console64_write_hex64((uint64_t)next_entry);
                early_console64_write("\n");
                if (round >= kExecRoundCap) {
                    early_console64_write("[x86_64][usermode] exec-chain cap reached, refusing further execve\n");
                    arch_x86_64_usermode_mark_exited(127);
                    break;
                }
                continue;
            }
            /* A2.P3-B-alpha: after parent SYS_EXIT, check whether a fork
             * was queued. If so, re-enter usermode_run(0) on the fork-
             * resume path. usermode_run() reads PCB.fork_pending and
             * builds PREPARED_USER_FRAME from the stashed trapframe
             * (rip = saved syscall-return PC, rsp = saved user rsp,
             *  rax cleared to 0 by iretq_enter_user). entry=0 is fine
             * because the fork-resume branch ignores entry. */
            {
                x86_64_proc_t *pf = arch_x86_64_proc_current();
                if (pf != NULL && pf->fork_pending) {
                    ++fork_rounds;
                    early_console64_write("[x86_64][usermode] fork round=");
                    early_console64_write_hex64((uint64_t)(uint32_t)fork_rounds);
                    early_console64_write("\n");
                    if (fork_rounds > kForkRoundCap) {
                        early_console64_write("[x86_64][usermode] fork-chain cap reached, dropping pending fork\n");
                        pf->fork_pending = 0;
                        break;
                    }
                    /* usermode_run() resets usermode_exited at entry. */
                    next_entry = 0;
                    continue;
                }
            }
            break;
        }
        early_console64_write("[x86_64][user] ring3 returned exit_code=");
        early_console64_write_hex64((uint64_t)(uint32_t)arch_x86_64_usermode_exit_code());
        early_console64_write(" exited=");
        early_console64_write_hex64(arch_x86_64_usermode_has_exited());
        early_console64_write(" exec_rounds=");
        early_console64_write_hex64((uint64_t)(uint32_t)round);
        early_console64_write("\n");
    } else {
        early_console64_write("[x86_64][user] initial-program load failed status=");
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

    /* ============================================================
     * GUI 桌面启动 (范围 B: i386 图形桌面移植)
     *
     * 至此 x86_64 主线已完成 selftest / usermode / shell 自检。
     * 以前这里是 for(;;) hlt —— 因此只有满屏文字、无桌面。
     * 现在拉起 GUI 桌面：
     *   1. window_manager_start_desktop() 初始化图形子系统(fb/font/wm)
     *      并绘制桌面背景 + 任务栏。
     *   2. 主循环反复调用 window_manager_poll() 处理鼠标/重绘，
     *      hlt 等中断、降低空转。
     * ============================================================ */
    early_console64_write("[x86_64][gui] starting desktop...\n");
    {
        /* 初始化 UEFI GOP framebuffer 后端（framebuffer64.c），
         * 从 early_framebuffer64_get_info() 取真实 GOP 分辨率/地址。
         * gui 层后续通过 framebuffer_* API 绘制。 */
        framebuffer_init();
        int gui_rc = window_manager_start_desktop();
        early_console64_write("[x86_64][gui] window_manager_start_desktop rc=");
        early_console64_write_hex64((uint64_t)(uint32_t)gui_rc);
        early_console64_write("\n");
        if (gui_rc == 0) {
            early_console64_write("[x86_64][gui] desktop up, entering poll loop\n");
            for (;;) {
                window_manager_poll();
                net_tick(0);   /* 驱动 TCP 重传定时器 + 收包轮询 */
                __asm__ __volatile__("hlt");
            }
        }
        /* 桌面启动失败：回退到原来的 hlt 循环（保证不崩）*/
        early_console64_write("[x86_64][gui] desktop start FAILED, fallback to hlt loop\n");
    }
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void kernel_main64(void) {
    kernel_main64_with_handoff(0);
}
