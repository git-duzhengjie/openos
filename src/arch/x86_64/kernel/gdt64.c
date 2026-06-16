#include "../include/gdt64.h"
#include "../include/tss64.h"

#define GDT64_ENTRY_COUNT 8u

#define GDT64_ACCESS_PRESENT 0x80u
#define GDT64_ACCESS_RING0   0x00u
#define GDT64_ACCESS_RING3   0x60u
#define GDT64_ACCESS_CODE    0x18u
#define GDT64_ACCESS_DATA    0x10u
#define GDT64_ACCESS_TSS     0x09u
#define GDT64_ACCESS_RW      0x02u

#define GDT64_FLAG_GRANULAR  0x8u
#define GDT64_FLAG_64BIT     0x2u
#define GDT64_FLAG_32BIT     0x4u

struct gdt64_pointer {
    uint16_t limit;
    x86_64_virt_addr_t base;
} __attribute__((packed));

static uint64_t gdt64[GDT64_ENTRY_COUNT] __attribute__((aligned(16)));
static struct gdt64_pointer gdt64_ptr;

static uint64_t make_gdt64_descriptor(uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    uint64_t descriptor = 0;
    descriptor |= (uint64_t)(limit & 0xFFFFu);
    descriptor |= (uint64_t)(base & 0xFFFFFFu) << 16;
    descriptor |= (uint64_t)access << 40;
    descriptor |= (uint64_t)((limit >> 16) & 0x0Fu) << 48;
    descriptor |= (uint64_t)(flags & 0x0Fu) << 52;
    descriptor |= (uint64_t)((base >> 24) & 0xFFu) << 56;
    return descriptor;
}

static void set_gdt64_tss_descriptor(uint16_t selector, x86_64_virt_addr_t base, uint32_t limit) {
    uint32_t index = selector >> 3;
    const uint8_t access = GDT64_ACCESS_PRESENT | GDT64_ACCESS_RING0 | GDT64_ACCESS_TSS;

    if (index + 1u >= GDT64_ENTRY_COUNT) {
        return;
    }

    gdt64[index] = make_gdt64_descriptor((uint32_t)base, limit, access, 0);
    gdt64[index + 1u] = base >> 32;
}

static void build_gdt64(void) {
    const uint8_t kernel_code = GDT64_ACCESS_PRESENT | GDT64_ACCESS_RING0 | GDT64_ACCESS_CODE | GDT64_ACCESS_RW;
    const uint8_t kernel_data = GDT64_ACCESS_PRESENT | GDT64_ACCESS_RING0 | GDT64_ACCESS_DATA | GDT64_ACCESS_RW;
    const uint8_t user_code = GDT64_ACCESS_PRESENT | GDT64_ACCESS_RING3 | GDT64_ACCESS_CODE | GDT64_ACCESS_RW;
    const uint8_t user_data = GDT64_ACCESS_PRESENT | GDT64_ACCESS_RING3 | GDT64_ACCESS_DATA | GDT64_ACCESS_RW;

    gdt64[0] = 0;
    gdt64[1] = make_gdt64_descriptor(0, 0, kernel_code, GDT64_FLAG_64BIT);
    gdt64[2] = make_gdt64_descriptor(0, 0, kernel_data, 0);
    gdt64[3] = make_gdt64_descriptor(0, 0, user_data, 0);
    gdt64[4] = make_gdt64_descriptor(0, 0, user_code, GDT64_FLAG_64BIT);
    gdt64[5] = make_gdt64_descriptor(0, 0xFFFFFu, user_code, GDT64_FLAG_GRANULAR | GDT64_FLAG_32BIT);
    set_gdt64_tss_descriptor(OPENOS_X86_64_GDT_TSS, arch_x86_64_tss_base(), arch_x86_64_tss_limit());

    gdt64_ptr.limit = (uint16_t)(sizeof(gdt64) - 1u);
    gdt64_ptr.base = (x86_64_virt_addr_t)(uintptr_t)&gdt64[0];
}

void arch_x86_64_gdt_init(void) {
    build_gdt64();
    __asm__ __volatile__(
        "lgdt %0\n"
        "movw %[data], %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        : "m"(gdt64_ptr), [data] "i"(OPENOS_X86_64_GDT_KERNEL_DATA)
        : "rax", "memory");
}

uint16_t arch_x86_64_gdt_kernel_code_selector(void) {
    return OPENOS_X86_64_GDT_KERNEL_CODE;
}

uint16_t arch_x86_64_gdt_kernel_data_selector(void) {
    return OPENOS_X86_64_GDT_KERNEL_DATA;
}

uint16_t arch_x86_64_gdt_user_code_selector(void) {
    return OPENOS_X86_64_GDT_USER_CODE | 3u;
}

uint16_t arch_x86_64_gdt_user32_code_selector(void) {
    return OPENOS_X86_64_GDT_USER32_CODE | 3u;
}

uint16_t arch_x86_64_gdt_user_data_selector(void) {
    return OPENOS_X86_64_GDT_USER_DATA | 3u;
}

uint16_t arch_x86_64_gdt_tss_selector(void) {
    return OPENOS_X86_64_GDT_TSS;
}
