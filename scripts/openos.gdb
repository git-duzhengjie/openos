# OpenOS default GDB command file.
# Loaded by scripts/gdb-openos.sh after it starts QEMU with -S -gdb.
# The helper script performs `target remote`; this file only configures the
# debugging session and helper commands so it can be parsed offline.

set confirm off
set pagination off
set disassembly-flavor intel
set print pretty on
set architecture i386

define openos-break-early
    break kmain
    break kernel_main
    break panic
    break page_fault_handler
end
document openos-break-early
Set useful early-kernel breakpoints for OpenOS i386 debugging.
end

define openos-regs
    info registers
    x/16wx $esp
    x/8i $eip
end
document openos-regs
Show registers, a stack snapshot, and instructions at EIP.
end

define openos-continue
    continue
end
document openos-continue
Continue execution after attaching to the paused QEMU target.
end

openos-break-early
printf "OpenOS GDB session ready. Use 'openos-continue' or 'continue'.\n"
