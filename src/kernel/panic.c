#include "include/panic.h"
#include "include/serial.h"

static panic_dump_t g_last_panic_dump;

static const char *panic_safe_text(const char *text)
{
    return text ? text : "unknown";
}

static void panic_copy_text(char *dst, const char *src)
{
    uint32_t i;

    if (!dst) {
        return;
    }

    src = panic_safe_text(src);
    for (i = 0; i + 1 < PANIC_TEXT_LEN && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static uint32_t panic_read_cr3(void)
{
    uint32_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (value));
    return value;
}

static void panic_write_field_hex(const char *name, uint32_t value)
{
    serial_write(name);
    serial_write("=");
    serial_write_hex(value);
    serial_write(" ");
}

static void panic_dump_stack_words(const char *label, uint32_t addr, uint32_t count)
{
    if (!addr) {
        return;
    }

    serial_write("[PANIC] ");
    serial_write(label);
    serial_write(" base=");
    serial_write_hex(addr);
    serial_write("\n");

    uint32_t *words = (uint32_t *)addr;
    for (uint32_t i = 0; i < count; ++i) {
        if ((i & 3u) == 0) {
            serial_write("[PANIC]   +");
            serial_write_hex(i * 4u);
            serial_write(": ");
        }
        serial_write_hex(words[i]);
        serial_write(" ");
        if ((i & 3u) == 3u) {
            serial_write("\n");
        }
    }
    if ((count & 3u) != 0) {
        serial_write("\n");
    }
}

const panic_dump_t *panic_get_last_dump(void)
{
    return &g_last_panic_dump;
}

uint32_t panic_dump_is_valid(const panic_dump_t *dump)
{
    return dump && dump->magic == PANIC_DUMP_MAGIC &&
           dump->version == PANIC_DUMP_VERSION && dump->valid;
}

void panic_capture_dump(const panic_frame_t *frame)
{
    const registers_t *regs = frame ? frame->regs : 0;
    panic_dump_t *dump = &g_last_panic_dump;

    dump->magic = PANIC_DUMP_MAGIC;
    dump->version = PANIC_DUMP_VERSION;
    dump->valid = 1;
    panic_copy_text(dump->arch, frame ? frame->arch : 0);
    panic_copy_text(dump->reason, frame ? frame->reason : 0);
    dump->pid = frame ? frame->pid : 0;
    dump->fault_addr = frame ? frame->fault_addr : 0;
    dump->has_fault_addr = frame ? frame->has_fault_addr : 0;
    dump->cr3 = panic_read_cr3();

    if (!regs) {
        return;
    }

    dump->int_no = regs->int_no;
    dump->err_code = regs->err_code;
    dump->eip = regs->eip;
    dump->cs = regs->cs;
    dump->eflags = regs->eflags;
    dump->saved_esp = regs->esp_skip;
    dump->user_esp = regs->user_esp;
    dump->user_ss = regs->user_ss;
    dump->eax = regs->eax;
    dump->ebx = regs->ebx;
    dump->ecx = regs->ecx;
    dump->edx = regs->edx;
    dump->esi = regs->esi;
    dump->edi = regs->edi;
    dump->ebp = regs->ebp;
    dump->ds = regs->ds;
    dump->es = regs->es;
    dump->fs = regs->fs;
    dump->gs = regs->gs;
}

void panic_log_exception(const panic_frame_t *frame)
{
    const registers_t *regs = frame ? frame->regs : 0;
    const panic_dump_t *dump;

    panic_capture_dump(frame);
    dump = panic_get_last_dump();

    serial_write("\n[PANIC] OpenOS kernel panic\n");
    serial_write("[PANIC] arch=");
    serial_write(panic_safe_text(frame ? frame->arch : 0));
    serial_write(" reason=");
    serial_write(panic_safe_text(frame ? frame->reason : 0));
    serial_write("\n");

    if (regs) {
        serial_write("[PANIC] vector ");
        panic_write_field_hex("int", regs->int_no);
        panic_write_field_hex("err", regs->err_code);
        if (frame && frame->has_fault_addr) {
            panic_write_field_hex("cr2", frame->fault_addr);
        }
        serial_write("\n");

        serial_write("[PANIC] ip     ");
        panic_write_field_hex("eip", regs->eip);
        panic_write_field_hex("cs", regs->cs);
        panic_write_field_hex("eflags", regs->eflags);
        serial_write("\n");

        serial_write("[PANIC] stack  ");
        panic_write_field_hex("saved_esp", regs->esp_skip);
        panic_write_field_hex("user_esp", regs->user_esp);
        panic_write_field_hex("user_ss", regs->user_ss);
        serial_write("\n");

        serial_write("[PANIC] regs   ");
        panic_write_field_hex("eax", regs->eax);
        panic_write_field_hex("ebx", regs->ebx);
        panic_write_field_hex("ecx", regs->ecx);
        panic_write_field_hex("edx", regs->edx);
        serial_write("\n");

        serial_write("[PANIC] regs   ");
        panic_write_field_hex("esi", regs->esi);
        panic_write_field_hex("edi", regs->edi);
        panic_write_field_hex("ebp", regs->ebp);
        serial_write("\n");

        serial_write("[PANIC] seg    ");
        panic_write_field_hex("ds", regs->ds);
        panic_write_field_hex("es", regs->es);
        panic_write_field_hex("fs", regs->fs);
        panic_write_field_hex("gs", regs->gs);
        serial_write("\n");

        panic_dump_stack_words("kstack", regs->esp_skip, 32u);
        panic_dump_stack_words("frame", (uint32_t)regs, 24u);
    }

    serial_write("[PANIC] proc   pid=");
    serial_write_hex(frame ? frame->pid : 0);
    serial_write("\n");

    if (panic_dump_is_valid(dump)) {
        serial_write("[PANIC] dump   ");
        panic_write_field_hex("magic", dump->magic);
        panic_write_field_hex("version", dump->version);
        panic_write_field_hex("cr3", dump->cr3);
        serial_write("\n");
    }

    serial_write("[PANIC] system halted\n");
}

void panic_halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void panic_log_and_halt(const panic_frame_t *frame)
{
    panic_log_exception(frame);
    panic_halt();
}
