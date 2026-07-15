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
#include "blockdev.h"
#include "pci.h"
#include "virtio_net.h"
#include "virtio_gpu.h"
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
#include "../include/ahci64.h"
#include "../include/ext4_64.h"
#include "../include/nvme64.h"
#include "../include/security64.h"
#include "../include/xhci64.h"
#include "../../../kernel/include/sound.h"
#include "../include/tsc64.h"
#include "../include/tsc_selftest64.h"
#include "../include/irq_selftest64.h"
#include "../include/sched_preempt_selftest64.h"
#include "../include/apic_selftest64.h"
#include "../include/acpi_selftest64.h"
#include "../include/power64.h"
#include "../include/power_selftest64.h"
#include "../include/cpufreq64.h"
#include "../include/cpufreq_selftest64.h"
#include "../include/cred_selftest64.h"
#include "../include/login_selftest64.h"
#include "../include/gfx_selftest64.h"
#include "../include/virtio_gpu_selftest64.h"
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
extern void virtio_input_init(void);  /* virtio-input 键鼠驱动初始化 (virtio_input64.c) */
extern void virtio_input_poll(void);  /* virtio-input eventq 轮询注入 GUI */
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

/* ext4_read_fn 适配器：将 ext 驱动的 32 位 LBA 回调转发到 AHCI 的 64 位读接口 */
static int ext4_ahci_read_adapter(uint32_t lba, uint32_t count, void *buf) {
    return ahci_read_sectors((uint64_t)lba, count, buf);
}

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
    /* M6.4 安全加固: 探测 CPU 安全能力, 启用 SMEP (禁止内核执行用户页),
     * 初始化栈保护 canary, 强制 W^X。M6.4.4: SMAP 现已上线——syscall 汇编
     * 入口已用 STAC/CLAC 包裹 dispatch 对用户内存的访问(粗粒度收口)。 */
    arch_x86_64_security_probe();
    arch_x86_64_stack_guard_init();
    arch_x86_64_security_enable_smep();
    arch_x86_64_security_enforce_wxorx();
    arch_x86_64_security_enable_smap();
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
    /* Step M6.4: virtio-gpu 2D 驱动。查找 1af4:1050（modern-only），
     * 走 modern PCI transport 建立 controlq，跑 GET_DISPLAY_INFO ->
     * CREATE_2D -> ATTACH_BACKING -> SET_SCANOUT 建立 2D 管线。
     * 无设备时安全跳过（device_count == 0）。 */
    virtio_gpu_init();
    /* Step M6.9: virtio-input 键盘/鼠标驱动。查找 1af4:1052（可多设备），
     * 建立 eventq，预投递 write-only 事件 buffer，进入 DRIVER_OK。
     * evdev 事件在 GUI poll loop 中经 virtio_input_poll() 翻译并注入。
     * 无设备时安全跳过（PS/2 继续工作）。 */
    virtio_input_init();
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
    /* M1.6 自检：真·联网 —— DNS + TCP 三次握手 + HTTP GET + 收响应。
     * 走 QEMU user 网络 NAT 出口，能连到宿主机真实网络。
     * wrapper 内部保存/恢复 IF；此处 PIC 尚未 remap，IF 由 wrapper 托管。 */
    {
        extern void early_serial64_write(const char *s);
        extern int net_http_get_selftest(const char *host, const char *path);
        early_serial64_write("[net] 自检：真·联网 HTTP GET http://example.com/ ...\n");
        int hr = net_http_get_selftest("example.com", "/");
        early_serial64_write(hr >= 0
            ? "[net] HTTP PASS: 真·联网成功，已收到 HTTP 响应\n"
            : "[net] HTTP 未完成(无外网应答属正常，见上方阶段码)\n");
    }
    /* M1.7 自检：ring3 TCP blocking API —— 直接调用 net_tcp_connect/send/recv/close
     * blocking 封装(与 SYS_TCP_* 系统调用同一条链路)，向 example.com:80 发一次真实
     * HTTP GET 并收响应。验证用户态 wget 依赖的内核 TCP 出口链路可用。 */
    {
        extern void early_serial64_write(const char *s);
        extern int net_dns_resolve(const char *hostname, uint32_t *out_ip);
        extern int net_tcp_connect_blocking(uint32_t dst_ip, uint16_t dst_port);
        extern int net_tcp_send_blocking(int conn_id, const uint8_t *data, uint16_t len);
        extern int net_tcp_recv_blocking(int conn_id, uint8_t *buf, uint16_t len, uint32_t poll_loops);
        extern int net_tcp_close_blocking(int conn_id);
        early_serial64_write("[net] 自检：M1.7 TCP blocking API 直连 example.com:80 ...\n");
        uint32_t hip = 0;
        if (net_dns_resolve("example.com", &hip) == 0 && hip != 0) {
            int conn = net_tcp_connect_blocking(hip, 80);
            if (conn >= 0) {
                early_serial64_write("[net] M1.7 TCP: 三次握手完成 (ESTABLISHED)\n");
                static const char req[] =
                    "GET / HTTP/1.1\r\nHost: example.com\r\n"
                    "Connection: close\r\nUser-Agent: openos-m17/1.0\r\n\r\n";
                int sent = net_tcp_send_blocking(conn, (const uint8_t *)req,
                                                 (uint16_t)(sizeof(req) - 1));
                if (sent > 0) {
                    early_serial64_write("[net] M1.7 TCP: HTTP GET 已发出\n");
                    static uint8_t rbuf[1024];
                    int total = 0, empty = 0;
                    for (int i = 0; i < 20; i++) {
                        int g = net_tcp_recv_blocking(conn, rbuf, (uint16_t)sizeof(rbuf), 120);
                        if (g > 0) { total += g; empty = 0; }
                        else if (++empty >= 3) break;
                    }
                    early_serial64_write(total > 0
                        ? "[net] M1.7 TCP PASS: 收到 HTTP 响应(见 net.pcap)\n"
                        : "[net] M1.7 TCP: 未收到响应(无外网应答属正常)\n");
                } else {
                    early_serial64_write("[net] M1.7 TCP: 发送失败\n");
                }
                net_tcp_close_blocking(conn);
            } else {
                early_serial64_write("[net] M1.7 TCP: 连接失败(无外网应答属正常)\n");
            }
        } else {
            early_serial64_write("[net] M1.7 TCP: DNS 未解析，跳过\n");
        }
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

    /* M2.1：AHCI/SATA 驱动自测（读写回校验） */
    if (ahci_selftest() == 0) {
        early_console64_write("[x86_64][ahci] SATA disk selftest PASS\n");
    } else {
        early_console64_write("[x86_64][ahci] SATA disk selftest skipped/FAIL\n");
    }

    /* M2.2：NVMe 驱动自测（IDENTIFY + 读写回校验） */
    if (nvme_init() == 0 && nvme_selftest() == 0) {
        early_console64_write("[x86_64][nvme] NVMe disk selftest PASS\n");
    } else {
        early_console64_write("[x86_64][nvme] NVMe disk selftest skipped/FAIL\n");
    }

    /* M2.3：xHCI USB 主机控制器自测（探测 + 命令环 NOOP + 端口枚举） */
    if (xhci_init() == 0 && xhci_selftest() == 0) {
        early_console64_write("[x86_64][xhci] xHCI selftest PASS\n");
        /* M2.3 Step3-4：枚举 HID 键鼠，配置 Interrupt-IN 端点并注册 input 设备 */
        usb_hid_init();
        /* M2.3 Step5：枚举 USB 大容量存储(U 盘)，配置 Bulk 端点并初始化 SCSI */
        usb_msc_init();
    } else {
        early_console64_write("[x86_64][xhci] xHCI selftest skipped/FAIL\n");
    }

    /* M2.4：声卡 / 音频子系统（PCI 探测 AC97/HDA + PC Speaker + AC97 PCM 播放自检） */
    sound_init();

    /* M2.2：硬件探测完成后，将各就绪驱动注册进块设备抽象层（nvme0/sda/hda/hdb）。
     * 上层（未来 VFS/工具）可经 blockdev_find(name) 做设备无关的读写。 */
    blockdev_init();
    {
        int bd_n = blockdev_register_hw_devices();
        if (bd_n > 0)
            early_console64_write("[x86_64][blockdev] registered hw block devices\n");
        else
            early_console64_write("[x86_64][blockdev] no hw block device registered\n");
    }

    /* 阶段 4-1：探测并挂载 FAT32 数据盘。
     * 优先尝试 NVMe（现代存储，块大小=512 才可直通），
     * 否则回退到 ATA secondary slave。与 master 持久化盘隔离。 */
    int fat_mounted = 0;
    if (nvme_present() && nvme_block_size() == 512) {
        early_console64_write("[x86_64][nvme] trying FAT32 mount on NVMe...\n");
        if (fat32_mount(nvme_fat_read, 0) == 0) {
            early_console64_write("[x86_64][fat32] mounted at /mnt/fat (NVMe)\n");
            fat32_set_write_fn(nvme_fat_write);
            if (fat32_writable())
                early_console64_write("[x86_64][fat32] write enabled (RW)\n");
            fat32_selftest();
            fat_mounted = 1;
        } else {
            early_console64_write("[x86_64][fat32] NVMe mount failed, fallback to ATA\n");
        }
    }
    if (!fat_mounted) {
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
    }

    /* 阶段 M3.2：挂载 AHCI/SATA 盘上的 ext2/ext4 只读文件系统。
     * AHCI 盘（openos-ahci.img）整盘格式化为 ext2，含 hello.txt/subdir 等测试数据。 */
    if (ahci_present()) {
        int er = ext4_mount(ext4_ahci_read_adapter, 0);
        if (er == 0) {
            early_console64_write("[x86_64][ext] mounted ext fs on AHCI disk\n");
            ext4_selftest();
        } else {
            early_console64_write("[x86_64][ext] ext mount failed\n");
        }
    } else {
        early_console64_write("[x86_64][ext] no AHCI disk, skip ext mount\n");
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

    /* 阶段 4-3（M3.3）：验证 ext2/ext4 已接入统一 VFS 挂载点 /mnt/ext，
     * 与 /mnt/fat 走同一套 vfs_* int-fd 接口（readdir/stat/open/read）。 */
    if (ext4_mounted()) {
        early_console64_write("[x86_64][ext-vfs] === /mnt/ext selftest ===\n");
        /* 1) readdir 根目录 */
        for (int i = 0; ; i++) {
            dentry_t *de = vfs_readdir("/mnt/ext", i);
            if (!de) break;
            early_console64_write("[x86_64][ext-vfs] entry: ");
            early_console64_write(de->name);
            if (de->inode && (de->inode->mode & FS_DIR))
                early_console64_write("  <DIR>");
            early_console64_write("\n");
            if (i > 32) break;
        }
        /* 2) stat + open + read /mnt/ext/hello.txt */
        {
            inode_t st;
            if (vfs_stat("/mnt/ext/hello.txt", &st) == 0) {
                early_console64_write("[x86_64][ext-vfs] stat hello.txt size=");
                early_console64_write_hex64((uint64_t)(uint32_t)st.size);
                early_console64_write("\n");
            }
            int fd = vfs_open("/mnt/ext/hello.txt", O_RDONLY, 0);
            if (fd >= 0) {
                char buf[128];
                int rn = vfs_read(fd, buf, sizeof(buf) - 1);
                if (rn > 0) {
                    buf[rn] = 0;
                    early_console64_write("[x86_64][ext-vfs] read hello.txt: ");
                    early_console64_write(buf);
                    early_console64_write("\n");
                }
                vfs_close(fd);
            } else {
                early_console64_write("[x86_64][ext-vfs] open hello.txt FAILED\n");
            }
        }
        /* 3) 嵌套子目录路径解析 /mnt/ext/subdir/inside.txt */
        {
            int fd = vfs_open("/mnt/ext/subdir/inside.txt", O_RDONLY, 0);
            if (fd >= 0) {
                char buf[128];
                int rn = vfs_read(fd, buf, sizeof(buf) - 1);
                if (rn > 0) {
                    buf[rn] = 0;
                    early_console64_write("[x86_64][ext-vfs] read subdir/inside.txt: ");
                    early_console64_write(buf);
                    early_console64_write("\n");
                }
                vfs_close(fd);
            }
        }
        early_console64_write("[x86_64][ext-vfs] === selftest done ===\n");
    }

    /* ---- M3.4 文件权限模型 selftest ----
     * 直接验证 vfs_check_perm() 纯函数（构造 inode + 模拟不同 uid/gid），
     * 以及 vfs_chmod/vfs_chown 在 ramfs 上的实际生效。 */
    {
        early_console64_write("[x86_64][perm] === M3.4 permission model selftest ===\n");
        int pass = 0, fail = 0;
        /* 构造一个属主 uid=1000 gid=1000、mode=0640 的普通文件 inode */
        inode_t ino; for (unsigned i=0;i<sizeof(ino);i++) ((char*)&ino)[i]=0;
        ino.mode = FS_FILE | 0640;   /* rw-r----- */
        ino.uid = 1000; ino.gid = 1000;
        /* owner(1000,1000) 可读可写不可执行 */
        if (vfs_check_perm(&ino,1000,1000,VFS_MAY_READ)==0) pass++; else fail++;
        if (vfs_check_perm(&ino,1000,1000,VFS_MAY_WRITE)==0) pass++; else fail++;
        if (vfs_check_perm(&ino,1000,1000,VFS_MAY_EXEC)!=0) pass++; else fail++;
        /* group(2000,1000) 只读 */
        if (vfs_check_perm(&ino,2000,1000,VFS_MAY_READ)==0) pass++; else fail++;
        if (vfs_check_perm(&ino,2000,1000,VFS_MAY_WRITE)!=0) pass++; else fail++;
        /* other(2000,2000) 无任何权限 */
        if (vfs_check_perm(&ino,2000,2000,VFS_MAY_READ)!=0) pass++; else fail++;
        /* root(uid=0) 读写均过，但无 x 位时执行被拒 */
        if (vfs_check_perm(&ino,0,0,VFS_MAY_READ|VFS_MAY_WRITE)==0) pass++; else fail++;
        if (vfs_check_perm(&ino,0,0,VFS_MAY_EXEC)!=0) pass++; else fail++;
        /* 目录对 root 总可搜索（即使 mode 无 x） */
        inode_t dino=ino; dino.mode=FS_DIR|0600;
        if (vfs_check_perm(&dino,0,0,VFS_MAY_EXEC)==0) pass++; else fail++;
        early_console64_write("[x86_64][perm] check_perm pass=");
        early_console64_write_hex64((uint64_t)(uint32_t)pass);
        early_console64_write(" fail=");
        early_console64_write_hex64((uint64_t)(uint32_t)fail);
        early_console64_write("\n");
        /* 在 ramfs 实体上验证 chmod/chown 生效（当前为 root） */
        {
            int fd = vfs_open("/perm_test.txt", O_WRONLY|O_CREAT, 0644);
            if (fd >= 0) {
                vfs_write(fd, "hi", 2);
                vfs_close(fd);
                inode_t st;
                if (vfs_chmod("/perm_test.txt", 0600)==0 &&
                    vfs_stat("/perm_test.txt",&st)==0 && (st.mode & 0xFFF)==0600) {
                    early_console64_write("[x86_64][perm] CHMOD VERIFY OK (0600)\n");
                } else {
                    early_console64_write("[x86_64][perm] CHMOD VERIFY FAIL\n");
                }
                if (vfs_chown("/perm_test.txt", 1234, 5678)==0 &&
                    vfs_stat("/perm_test.txt",&st)==0 && st.uid==1234 && st.gid==5678) {
                    early_console64_write("[x86_64][perm] CHOWN VERIFY OK (1234:5678)\n");
                } else {
                    early_console64_write("[x86_64][perm] CHOWN VERIFY FAIL\n");
                }
                vfs_unlink("/perm_test.txt");
            }
        }
        early_console64_write("[x86_64][perm] === selftest done ===\n");
    }

    /* ============ M3.5 链接（硬链接/软链接）selftest ============ */
    {
        early_console64_write("[x86_64][link] === M3.5 link selftest ===\n");
        int lp = 0, lf = 0;
        /* 建源文件 */
        int fd = vfs_open("/link_src.txt", O_WRONLY|O_CREAT, 0644);
        if (fd >= 0) { vfs_write(fd, "LINKDATA", 8); vfs_close(fd); }
        /* --- 硬链接：建立后两名字读到同数据 --- */
        if (vfs_link("/link_src.txt", "/link_hard.txt") == 0) {
            char b[16]; int rn = -1;
            int hf = vfs_open("/link_hard.txt", O_RDONLY, 0);
            if (hf >= 0) { rn = vfs_read(hf, b, 8); vfs_close(hf); }
            if (rn == 8 && b[0]=='L' && b[7]=='A') lp++; else lf++;
            /* 通过硬链接写，源文件同步可见 */
            hf = vfs_open("/link_hard.txt", O_WRONLY, 0);
            if (hf >= 0) { vfs_write(hf, "XYZ", 3); vfs_close(hf); }
            int sf = vfs_open("/link_src.txt", O_RDONLY, 0);
            rn = -1; if (sf >= 0) { rn = vfs_read(sf, b, 3); vfs_close(sf); }
            if (rn == 3 && b[0]=='X' && b[2]=='Z') lp++; else lf++;
            /* 删源名，硬链接仍可读（nlinks递减不释放数据）*/
            vfs_unlink("/link_src.txt");
            hf = vfs_open("/link_hard.txt", O_RDONLY, 0);
            rn = -1; if (hf >= 0) { rn = vfs_read(hf, b, 3); vfs_close(hf); }
            if (rn == 3 && b[0]=='X') lp++; else lf++;
            vfs_unlink("/link_hard.txt");
        } else { lf += 3; }
        /* --- 软链接：创建 + readlink + 穿透访问 --- */
        fd = vfs_open("/sl_target.txt", O_WRONLY|O_CREAT, 0644);
        if (fd >= 0) { vfs_write(fd, "SOFTDATA", 8); vfs_close(fd); }
        if (vfs_symlink("/sl_target.txt", "/sl_link") == 0) {
            char lb[32]; int ln = vfs_readlink("/sl_link", lb, sizeof(lb));
            if (ln == 14 && lb[0]=='/' && lb[1]=='s') lp++; else lf++;
            /* 经软链接中间段穿透：/sl_link 作为目标——用目录软链接验证中间段展开 */
            vfs_mkdir("/sl_realdir", 0755);
            int df = vfs_open("/sl_realdir/deep.txt", O_WRONLY|O_CREAT, 0644);
            if (df >= 0) { vfs_write(df, "DEEP", 4); vfs_close(df); }
            if (vfs_symlink("/sl_realdir", "/sl_dirlink") == 0) {
                char db[8]; int dn = -1;
                int od = vfs_open("/sl_dirlink/deep.txt", O_RDONLY, 0);
                if (od >= 0) { dn = vfs_read(od, db, 4); vfs_close(od); }
                if (dn == 4 && db[0]=='D' && db[3]=='P') lp++; else lf++;
                vfs_unlink("/sl_dirlink");
            } else { lf++; }
            vfs_unlink("/sl_realdir/deep.txt");
            vfs_rmdir("/sl_realdir");
            vfs_unlink("/sl_link");
        } else { lf += 2; }
        vfs_unlink("/sl_target.txt");
        early_console64_write("[x86_64][link] link pass=");
        early_console64_write_hex64((uint64_t)(uint32_t)lp);
        early_console64_write(" fail=");
        early_console64_write_hex64((uint64_t)(uint32_t)lf);
        early_console64_write("\n[x86_64][link] === selftest done ===\n");
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
#ifndef M5_FAST_BOOT
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
#endif /* M5_FAST_BOOT: sched/net cooperative selftests are slow+flaky under
        * single-core QEMU; skip in fast-boot diag path to reach ring3 fast. */

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
#ifndef M5_FAST_BOOT
    (void)arch_x86_64_sched_preempt_selftest_run();
#endif /* M5_FAST_BOOT: preempt-selftest slot-switch/exit_self path is flaky
        * under single-core QEMU; skip in fast-boot diag path. */

    /* Step M6.2: probe CPU frequency / thermal state via CPUID-gated MSR
     * access (read-only; never changes the P-state). Placed BEFORE the ACPI
     * / power path since it only depends on CPUID + the already-calibrated
     * TSC, and must not sit behind the single-core-flaky preempt-selftest.
     * Non-fatal. */
    (void)arch_x86_64_cpufreq_init();
    (void)arch_x86_64_cpufreq_selftest_run();

    /* Step M6.11.1: verify the per-process POSIX credential set (real/
     * effective/saved uid+gid) and the setuid/setgid privilege rules. The
     * test drives the API against the current PCB and restores the pristine
     * root identity before returning, so the live kernel PCB is untouched.
     * Non-fatal. */
    (void)arch_x86_64_cred_selftest_run();

    /* Step M6.11.3: verify the login/session flow (authenticate against the
     * initrd /etc/passwd + /etc/shadow, then drop privilege and become a
     * session leader). The test snapshots and restores the pristine slot-0
     * root identity, so the live kernel PCB is untouched. Non-fatal. */
    (void)arch_x86_64_login_selftest_run();

    /* Step M6.3: verify the framebuffer row-blit acceleration primitive
     * (framebuffer_blit_row). framebuffer_init() is idempotent and only wires
     * up the already-existing UEFI GOP framebuffer, so calling it here is safe
     * even before the desktop starts. This probe only touches VRAM briefly,
     * restoring every pixel it reads. Kept before the single-core-flaky
     * preempt path region. Non-fatal. */
    framebuffer_init();
    (void)arch_x86_64_gfx_selftest_run();

    /* Step M6.4: virtio-gpu 2D driver selftest. Exercises the driver public
     * surface + present path when a virtio-gpu-pci device is attached; passes
     * as a no-op on GOP-only boots (graceful degradation). Non-fatal. */
    (void)arch_x86_64_virtio_gpu_selftest_run();

    /* Step G.3a: parse ACPI tables (RSDP via EFI cfg table -> XSDT -> MADT)
     * to enumerate CPUs and IO-APICs. Must run BEFORE apic_selftest so the
     * latter can later be evolved to use MADT-discovered LAPIC/IOAPIC bases
     * instead of the hard-coded MMIO addresses. Failure is non-fatal: the
     * G.1/G.2 path falls back to its compile-time defaults. */
    (void)arch_x86_64_acpi_selftest_run();

    /* Step M6.1a: parse the FADT + \_S5 for power management (shutdown /
     * reboot). Must run AFTER the ACPI parser has populated xsdt_phys /
     * rsdt_phys. Failure is non-fatal: power operations then fall back to
     * the legacy 8042 pulse (reboot) / QEMU debug-exit (shutdown). */
    (void)arch_x86_64_power_init();
    (void)arch_x86_64_power_selftest_run();
#ifdef M6_POWER_DIAG
    /* M6.1 end-to-end: actually trigger ACPI S5 soft-off. On QEMU this makes
     * the VM power off cleanly (exit rc 0) instead of hanging until timeout,
     * proving the PM1a_CNT write path works. Gated so normal boots never
     * self-destruct. */
    early_console64_write("\n[M6_POWER_DIAG] triggering ACPI shutdown...\n");
    arch_x86_64_power_shutdown();
#endif

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

    /* Step G.4.2 (M2.x): install storage MSI now that LAPIC is ready.
     * Storage init ran far earlier (before LAPIC), so drivers came up in
     * polling mode. Here we attach MSI vectors 0x30/0x31 and re-run the
     * selftest to exercise the interrupt-driven completion path. Both
     * install/selftest are non-fatal: polling fallback keeps working. */
    ahci_irq_install_late();
    nvme_irq_install_late();
    {
        early_console64_write("[x86_64][msi] re-verify storage via IRQ path...\n");
        /* 临时开中断：本阶段仍在主初始化流程（正式 sti 在后面），
         * 为验证 MSI 中断路径需临时允许外部中断递交。自测后恢复 cli。 */
        __asm__ __volatile__("sti");
        if (ahci_selftest() == 0)
            early_console64_write("[x86_64][msi] AHCI IRQ-path selftest PASS\n");
        else
            early_console64_write("[x86_64][msi] AHCI IRQ-path selftest FAIL\n");
        {
            uint32_t irqn = ahci_irq_count();
            if (irqn > 0)
                early_console64_write("[x86_64][msi] AHCI interrupts DID fire (MSI path live)\n");
            else
                early_console64_write("[x86_64][msi] AHCI no interrupt fired (polling fallback)\n");
        }
        if (nvme_selftest() == 0)
            early_console64_write("[x86_64][msi] NVMe IRQ-path selftest PASS\n");
        else
            early_console64_write("[x86_64][msi] NVMe IRQ-path selftest FAIL\n");
        {
            uint32_t nirq = nvme_irq_count();
            if (nirq > 0)
                early_console64_write("[x86_64][msi] NVMe interrupts DID fire (MSI-X path live)\n");
            else
                early_console64_write("[x86_64][msi] NVMe no interrupt fired (polling fallback)\n");
        }
        /* M2.3：xHCI MSI 晚期挂载 + IRQ 路径复测 */
        xhci_irq_install_late();
        if (xhci_selftest() == 0)
            early_console64_write("[x86_64][msi] xHCI IRQ-path selftest PASS\n");
        else
            early_console64_write("[x86_64][msi] xHCI IRQ-path selftest FAIL\n");
        {
            uint32_t xirq = xhci_irq_count();
            if (xirq > 0)
                early_console64_write("[x86_64][msi] xHCI interrupts DID fire (MSI path live)\n");
            else
                early_console64_write("[x86_64][msi] xHCI no interrupt fired (polling fallback)\n");
        }
        __asm__ __volatile__("cli");
    }

    /* Step G.2: priority-weighted scheduling self-test. Spawns three
     * spin kthreads at HIGH / NORMAL / LOW priorities and asserts the
     * canonical CPU-share ordering H > N > L. Runs through the
     * IOAPIC GSI2 path established by G.1 (falls back to PIC if LAPIC
     * isn't ready). Re-masks IRQ0 on exit so downstream ring3 path
     * keeps inheriting "IRQs-off" precondition. */
#ifndef M5_FAST_BOOT
    (void)arch_x86_64_sched_prio_selftest_run();
#endif /* M5_FAST_BOOT: prio-selftest uses slot-switch/exit_self timing that is
        * flaky under single-core QEMU; skip in fast-boot diag path. */

    /* ============================================================
     * GUI 桌面启动 —— 早期验证路径 (GUI_EARLY_VERIFY)
     *
     * 历史遗留：早期 do_wait 采用 pause 死自旋，BSP 空转饿死跑
     * child 的 AP，fork-multi 回收退化到分钟级，撞 headless 超时。
     * 已修复：do_wait 两处自旋改为 sti; hlt，让 BSP 让出 host
     * 时间片给 AP，child 的 do_exit 跨核 IPI / LAPIC tick 均可
     * 唤醒 BSP，回收降至毫秒级。fork-multi 三 child 现秒级全回收。
     * 此早期 GUI 拉起路径保留作桌面可视化验证入口。
     * ============================================================ */
#ifdef SHELL_LAUNCH_SELFTEST
    /* M1.5.3: ring3 network round-trip. Runs AFTER smp/prio self-tests so
     * that stage 19's "syscall_user_rsp == 0 at boot" invariant is not
     * violated by these user-mode syscalls firing early. */
    early_console64_write("[x86_64][net-r3] /bin/ifconfig ...\n");
    arch_x86_64_shell_exec_line("run /bin/ifconfig");
    early_console64_write("[x86_64][net-r3] /bin/nslookup example.com ...\n");
    arch_x86_64_shell_exec_line("run /bin/nslookup example.com");
    early_console64_write("[x86_64][net-r3] /bin/ping 10.0.2.2 3 ...\n");
    arch_x86_64_shell_exec_line("run /bin/ping 10.0.2.2 3");
    early_console64_write("[x86_64][net-r3] /bin/wget example.com / (ring3 TCP) ...\n");
    arch_x86_64_shell_exec_line("run /bin/wget example.com /");
    early_console64_write("[x86_64][net-r3] done\n");

    /* Route-2 end-to-end probe: exercise the shell 'run' path (which in
     * turn drives arch_x86_64_usermode_launch_path + VFS-first ELF load)
     * before the GUI desktop takes over the poll loop. Serial-only. */
    early_console64_write("[x86_64][r2-selftest] shell 'run /bin/hello64' ...\n");
    arch_x86_64_shell_exec_line("run /bin/hello64");
    early_console64_write("[x86_64][r2-selftest] done\n");
#endif

/* M5.1: when M5_RING3_CONSOLE is defined, skip the early-GUI lockscreen
 * dead-loop so control falls through to the launcher ring3 closed loop
 * below (line ~913). After ring3 finishes (launcher -> execve hello64_v2
 * -> exit), the second GUI block (also gated by GUI_EARLY_VERIFY) brings
 * up the desktop. This makes ring3 user output observable on serial in
 * headless builds while keeping the GUI demo path intact. */
#if defined(GUI_EARLY_VERIFY) && !defined(M5_RING3_CONSOLE)
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
#if defined(M6_CPUINFO_DIAG)
    /* M6.2d diag: jump straight to the CPU frequency / thermal self-test
     * (SYS_CPUINFO end-to-end). Single ring3 process, read-only, reliable
     * under single-core QEMU. Enabled only for -DM6_CPUINFO_DIAG builds. */
    const char *initial_path = "/bin/cpuinfo_selftest";
#elif defined(M5_OPKG_DIAG)
    /* M5.4d diag: jump straight to the package-manager end-to-end self-test
     * (install/list/info/remove closed loop). Single ring3 process, no
     * multithreaded exit_self path, so it is reliable under single-core QEMU.
     * Enabled only for -DM5_OPKG_DIAG builds; normal builds keep launcher. */
    const char *initial_path = "/bin/opkg_selftest";
#elif defined(M5_FAST_BOOT)
    /* M5.4c diag: jump straight to the opk install+run demo, bypassing the
     * launcher -> hello64_v2 -> thread_demo chain whose multithreaded
     * exit_self path is flaky under single-core QEMU. The full chain stays
     * the default for normal (non-fast-boot) builds. */
    const char *initial_path = "/bin/opk_demo";
#else
    const char *initial_path = "/bin/launcher";
#endif
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
        const int kExecRoundCap = 6; /* M5.4a: hello->fork->thread->libc->fs chain needs 5 */
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
                usb_hid_poll();   /* M2.3 Step3-4：USB HID 键鼠 report 轮询上报 */
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
