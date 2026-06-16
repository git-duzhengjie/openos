#include "unit_test.h"

#include <stddef.h>
#include <stdint.h>

#include "../../src/arch/x86_64/include/gdt64.h"
#include "../../src/arch/x86_64/include/tss64.h"

UNIT_TEST_CASE(gdt_selectors_are_stable)
{
    ASSERT_EQ_INT(0x00, OPENOS_X86_64_GDT_NULL);
    ASSERT_EQ_INT(0x08, OPENOS_X86_64_GDT_KERNEL_CODE);
    ASSERT_EQ_INT(0x10, OPENOS_X86_64_GDT_KERNEL_DATA);
    ASSERT_EQ_INT(0x18, OPENOS_X86_64_GDT_USER_DATA);
    ASSERT_EQ_INT(0x20, OPENOS_X86_64_GDT_USER_CODE);
    ASSERT_EQ_INT(0x28, OPENOS_X86_64_GDT_USER32_CODE);
    ASSERT_EQ_INT(0x30, OPENOS_X86_64_GDT_TSS);
}

UNIT_TEST_CASE(tss_layout_matches_x86_64_contract)
{
    ASSERT_EQ_SIZE(3u, OPENOS_X86_64_TSS_RSP_COUNT);
    ASSERT_EQ_SIZE(7u, OPENOS_X86_64_TSS_IST_COUNT);
    ASSERT_EQ_SIZE(16384u, OPENOS_X86_64_TSS_RSP0_STACK_SIZE);
    ASSERT_EQ_SIZE(8192u, OPENOS_X86_64_TSS_IST_STACK_SIZE);

    ASSERT_EQ_SIZE(104u, sizeof(struct x86_64_tss));
    ASSERT_EQ_SIZE(4u, offsetof(struct x86_64_tss, rsp));
    ASSERT_EQ_SIZE(36u, offsetof(struct x86_64_tss, ist));
    ASSERT_EQ_SIZE(102u, offsetof(struct x86_64_tss, iomap_base));
}

int main(void)
{
    UNIT_TEST_RUN(gdt_selectors_are_stable);
    UNIT_TEST_RUN(tss_layout_matches_x86_64_contract);
    return unit_test_finish();
}
