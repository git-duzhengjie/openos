#include "aarch64_exception.h"

#include "aarch64_uart.h"

#define AARCH64_ESR_EC_SHIFT 26u
#define AARCH64_ESR_EC_MASK  0x3fu
#define AARCH64_ESR_EC_SVC64 0x15u
#define AARCH64_INSN_SIZE    4u

static const char *aarch64_exception_name(unsigned long type)
{
    switch ((aarch64_exception_type_t)type) {
    case AARCH64_EXC_SYNC_CURRENT_SP0: return "sync current EL SP0";
    case AARCH64_EXC_IRQ_CURRENT_SP0: return "irq current EL SP0";
    case AARCH64_EXC_FIQ_CURRENT_SP0: return "fiq current EL SP0";
    case AARCH64_EXC_SERROR_CURRENT_SP0: return "serror current EL SP0";
    case AARCH64_EXC_SYNC_CURRENT_SPX: return "sync current EL SPx";
    case AARCH64_EXC_IRQ_CURRENT_SPX: return "irq current EL SPx";
    case AARCH64_EXC_FIQ_CURRENT_SPX: return "fiq current EL SPx";
    case AARCH64_EXC_SERROR_CURRENT_SPX: return "serror current EL SPx";
    case AARCH64_EXC_SYNC_LOWER_AARCH64: return "sync lower EL aarch64";
    case AARCH64_EXC_IRQ_LOWER_AARCH64: return "irq lower EL aarch64";
    case AARCH64_EXC_FIQ_LOWER_AARCH64: return "fiq lower EL aarch64";
    case AARCH64_EXC_SERROR_LOWER_AARCH64: return "serror lower EL aarch64";
    case AARCH64_EXC_SYNC_LOWER_AARCH32: return "sync lower EL aarch32";
    case AARCH64_EXC_IRQ_LOWER_AARCH32: return "irq lower EL aarch32";
    case AARCH64_EXC_FIQ_LOWER_AARCH32: return "fiq lower EL aarch32";
    case AARCH64_EXC_SERROR_LOWER_AARCH32: return "serror lower EL aarch32";
    default: return "unknown";
    }
}

static void aarch64_uart_write_hex_nibble(unsigned int value)
{
    value &= 0xfU;
    aarch64_uart_putc((char)(value < 10U ? ('0' + value) : ('a' + value - 10U)));
}

static void aarch64_uart_write_hex64(unsigned long value)
{
    aarch64_uart_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        aarch64_uart_write_hex_nibble((unsigned int)(value >> shift));
    }
}

static unsigned long aarch64_esr_ec(unsigned long esr)
{
    return (esr >> AARCH64_ESR_EC_SHIFT) & AARCH64_ESR_EC_MASK;
}

void aarch64_exception_init(void)
{
    unsigned long vector = (unsigned long)aarch64_exception_vector_table;
    __asm__ volatile("msr vbar_el1, %0\n"
                     "isb\n"
                     :
                     : "r"(vector)
                     : "memory");
    aarch64_syscall_init();
}

void aarch64_panic(const char *reason)
{
    aarch64_uart_write("[aarch64] PANIC: ");
    aarch64_uart_write(reason);
    aarch64_uart_write("\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}

static void aarch64_log_exception(const aarch64_trap_frame_t *frame)
{
    aarch64_uart_write("[aarch64] exception: ");
    aarch64_uart_write(aarch64_exception_name((unsigned long)frame->vector_id));
    aarch64_uart_write(" type=");
    aarch64_uart_write_hex64((unsigned long)frame->vector_id);
    aarch64_uart_write(" esr=");
    aarch64_uart_write_hex64((unsigned long)frame->esr_el1);
    aarch64_uart_write(" ec=");
    aarch64_uart_write_hex64(aarch64_esr_ec((unsigned long)frame->esr_el1));
    aarch64_uart_write(" elr=");
    aarch64_uart_write_hex64((unsigned long)frame->elr_el1);
    aarch64_uart_write(" far=");
    aarch64_uart_write_hex64((unsigned long)frame->far_el1);
    aarch64_uart_write(" spsr=");
    aarch64_uart_write_hex64((unsigned long)frame->spsr_el1);
    aarch64_uart_write("\n");
}

void aarch64_exception_dispatch(aarch64_trap_frame_t *frame)
{
    unsigned long ec;

    if (frame == 0) {
        aarch64_panic("null exception frame");
    }

    ec = aarch64_esr_ec((unsigned long)frame->esr_el1);

    if (frame->vector_id == AARCH64_EXC_SYNC_LOWER_AARCH64 && ec == AARCH64_ESR_EC_SVC64) {
        frame->x[0] = aarch64_syscall_dispatch(frame);
        frame->elr_el1 += AARCH64_INSN_SIZE;
        return;
    }

    switch ((aarch64_exception_type_t)frame->vector_id) {
    case AARCH64_EXC_IRQ_CURRENT_SP0:
    case AARCH64_EXC_IRQ_CURRENT_SPX:
    case AARCH64_EXC_IRQ_LOWER_AARCH64:
    case AARCH64_EXC_IRQ_LOWER_AARCH32:
        return;
    default:
        aarch64_log_exception(frame);
        aarch64_panic("unhandled exception");
        break;
    }
}
