#include "unit_test.h"

#include <stddef.h>
#include <stdint.h>

#include "../../src/kernel/include/panic.h"

UNIT_TEST_CASE(panic_dump_header_is_stable)
{
    ASSERT_EQ_INT(0x50444d50u, PANIC_DUMP_MAGIC);
    ASSERT_EQ_INT(1u, PANIC_DUMP_VERSION);
    ASSERT_EQ_SIZE(32u, PANIC_TEXT_LEN);

    ASSERT_EQ_SIZE(0u, offsetof(panic_dump_t, magic));
    ASSERT_EQ_SIZE(4u, offsetof(panic_dump_t, version));
    ASSERT_EQ_SIZE(8u, offsetof(panic_dump_t, valid));
    ASSERT_TRUE(offsetof(panic_dump_t, arch) < offsetof(panic_dump_t, reason));
    ASSERT_TRUE(offsetof(panic_dump_t, eip) < offsetof(panic_dump_t, eax));
}

UNIT_TEST_CASE(panic_dump_contains_core_registers)
{
    ASSERT_TRUE(sizeof(panic_dump_t) >= 128u);
    ASSERT_TRUE(offsetof(panic_dump_t, fault_addr) < offsetof(panic_dump_t, cr3));
    ASSERT_TRUE(offsetof(panic_dump_t, eflags) < offsetof(panic_dump_t, saved_esp));
    ASSERT_TRUE(offsetof(panic_dump_t, eax) < offsetof(panic_dump_t, gs));
}

int main(void)
{
    UNIT_TEST_RUN(panic_dump_header_is_stable);
    UNIT_TEST_RUN(panic_dump_contains_core_registers);
    return unit_test_finish();
}
