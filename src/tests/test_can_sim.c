#include "unity.h"
#include "can_bus.h"

void setUp(void) {
    // Vor jedem Test
}

void tearDown(void) {
    // Nach jedem Test
}

void test_can_init(void) {
    can_init();
    TEST_ASSERT_TRUE(1); // Placeholder
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_can_init);
    return UNITY_END();
}