#include <stdio.h>
#include "unity.h"

static void test_example(void)
{
    TEST_ASSERT_EQUAL(1, 1);
}

void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_example);
    UNITY_END();
}
