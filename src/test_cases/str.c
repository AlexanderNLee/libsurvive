#include "test_case.h"
#include "../survive_str.h"

TEST(SurviveUtils, Str) {
    cstring test = {};
    str_append(&test, "012345");
    ASSERT_EQ(test.d[test.length], 0);
    ASSERT_EQ(test.length, 6);

    str_append_printf(&test, "%8d", 12345);
    ASSERT_EQ(test.d[test.length], 0);
    ASSERT_EQ(test.length, 6 + 8);

    str_append_printf(&test, "%5000d", 67890);
    ASSERT_EQ(test.d[test.length], 0);
    ASSERT_EQ(test.d[test.length-1], '0');
    ASSERT_EQ(test.length, 6 + 8 + 5000);

    str_free(&test);
    return 0;
}