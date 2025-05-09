/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sandstone_p.h"

#if SANDSTONE_RESTRICTED_CMDLINE
#  define TEST_GROUP(name, descr)   .id = nullptr, .description = nullptr
#else
#  define TEST_GROUP(name, descr)   .id = name, .description = descr
#endif

__attribute__((section("test_group"), aligned(8)))
extern constexpr struct test_group group_compression = {
    TEST_GROUP("compression",
               "Tests that drive compression routines in various libraries"),
};

__attribute__((section("test_group"), aligned(8)))
extern constexpr struct test_group group_math = {
    TEST_GROUP("math",
               "Tests that perform math using, e.g., Eigen"),
};
