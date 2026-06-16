# OpenOS GDB helper script for QEMU gdbstub.
#
# Usage:
#   bash build.sh
#   qemu-system-i386 -drive format=raw,file=target/openos.img -m 512M \
#       -serial stdio -display none -S -s
#   gdb -q -x scripts/gdb-openos.gdb
#
# Environment variables:
#   OPENOS_GDB_KERNEL  Kernel ELF path, default: target/kernel.elf
#   OPENOS_GDB_HOST    QEMU gdbstub host, default: localhost
#   OPENOS_GDB_PORT    QEMU gdbstub port, default: 1234

set confirm off
set pagination off
set disassemble-next-line on
set print pretty on
set architecture i386

set breakpoint pending on

python
import os
kernel = os.environ.get("OPENOS_GDB_KERNEL", "target/kernel.elf")
host = os.environ.get("OPENOS_GDB_HOST", "localhost")
port = os.environ.get("OPENOS_GDB_PORT", "1234")
if os.path.exists(kernel):
    gdb.execute("file " + kernel)
else:
    print("OpenOS GDB: symbol file not found, skipping: {}".format(kernel))
gdb.execute("set $openos_gdb_target = \"{}:{}\"".format(host, port))
end

define openos-connect
    python
import os
host = os.environ.get("OPENOS_GDB_HOST", "localhost")
port = os.environ.get("OPENOS_GDB_PORT", "1234")
gdb.execute("target remote {}:{}".format(host, port))
    end
end
document openos-connect
Connect to the QEMU gdbstub. Defaults to localhost:1234.
Override with OPENOS_GDB_HOST and OPENOS_GDB_PORT.
end

define openos-break-boot
    break kernel_main
    break idt_init
    break isr_handler
    break page_fault_handler
end
document openos-break-boot
Install common OpenOS boot and exception breakpoints.
end

define openos-regs
    info registers
    x/16wx $esp
    x/8i $eip
end
document openos-regs
Show registers, top of stack, and instructions at EIP.
end

define openos-panic-dump
    p/x g_last_panic_dump
end
document openos-panic-dump
Print the in-kernel last panic dump if symbols are available.
end

define openos-help
    printf "OpenOS GDB commands:\n"
    printf "  openos-connect     Connect to QEMU gdbstub\n"
    printf "  openos-break-boot  Set common boot/exception breakpoints\n"
    printf "  openos-regs        Show registers, stack, and EIP disassembly\n"
    printf "  openos-panic-dump  Print last panic dump symbol\n"
end

document openos-help
Show OpenOS GDB helper commands.
end

openos-help
