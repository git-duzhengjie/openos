#include "aarch64_exception.h"

#include "aarch64_uart.h"
#include "aarch64_gicv3.h"

#define AARCH64_ESR_EC_SHIFT 26u
#define AARCH64_ESR_EC_MASK  0x3fu
#define AARCH64_ESR_EC_SVC64 0x15u
#define AARCH64_INSN_SIZE    4u

#define AARCH64_IRQ_TABLE_SIZE 128u

typedef struct aarch64_irq_slot {
    aarch64_irq_handler_fn_t handler;
    void *cookie;
    uint32_t count;
} aarch64_irq_slot_t;

static aarch64_irq_slot_t g_aarch64_irq_table[AARCH64_IRQ_TABLE_SIZE];
static uint32_t           g_aarch64_irq_total;
static uint32_t           g_aarch64_irq_spurious;

int aarch64_irq_register(uint32_t intid,
                         aarch64_irq_handler_fn_t handler,
                         void *cookie)
{
    if (intid >= AARCH64_IRQ_TABLE_SIZE || handler == 0) {
        return -1;
    }
    g_aarch64_irq_table[intid].handler = handler;
    g_aarch64_irq_table[intid].cookie = cookie;
    return 0;
}

uint32_t aarch64_irq_total_count(void)
{
    return g_aarch64_irq_total;
}

uint32_t aarch64_irq_count_for(uint32_t intid)
{
    if (intid >= AARCH64_IRQ_TABLE_SIZE) {
        return 0;
    }
    return g_aarch64_irq_table[intid].count;
}

uint32_t aarch64_irq_spurious_count(void)
{
    return g_aarch64_irq_spurious;
}

/* Software trigger for selftest / initial bring-up (no real IRQ). */
void aarch64_irq_simulate(uint32_t intid)
{
    if (intid >= AARCH64_IRQ_TABLE_SIZE) {
        g_aarch64_irq_spurious++;
        return;
    }
    aarch64_irq_slot_t *slot = &g_aarch64_irq_table[intid];
    if (slot->handler == 0) {
        g_aarch64_irq_spurious++;
        return;
    }
    slot->count++;
    g_aarch64_irq_total++;
    slot->handler(intid, slot->cookie);
}

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
    {
        uint32_t iar = aarch64_gicv3_ack_irq();
        uint32_t intid = iar & 0xFFFFFFu;
        if (intid >= 1020u) {
            /* Spurious. */
            g_aarch64_irq_spurious++;
            (void)aarch64_gicv3_eoi_irq(intid);
            return;
        }
        if (intid < AARCH64_IRQ_TABLE_SIZE && g_aarch64_irq_table[intid].handler) {
            g_aarch64_irq_table[intid].count++;
            g_aarch64_irq_total++;
            g_aarch64_irq_table[intid].handler(intid, g_aarch64_irq_table[intid].cookie);
        } else {
            g_aarch64_irq_spurious++;
        }
        (void)aarch64_gicv3_eoi_irq(intid);
        return;
    }
    default:
        aarch64_log_exception(frame);
        aarch64_panic("unhandled exception");
        break;
    }
}
