#include "persistent_queue.h"
#include "transport_tests.h"
#include "pack_test.h"

#include <gtest/gtest.h>

GTEST_API_ int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
