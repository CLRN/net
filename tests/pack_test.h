#include "net/details/params.hpp"

#include <gtest/gtest.h>

TEST(Packer, Unpack)
{
    int var = 1;
    int t = hlp::Param<int>::Unpack('d', "dsdsa", var, 1);
    EXPECT_EQ(t, var);
    hlp::Param<int>::Unpack(var) = 10;
    EXPECT_EQ(10, var);
}


TEST(Packer, Default)
{
    EXPECT_EQ(hlp::Param<int>::Unpack('a', "dsa"), 0);
    EXPECT_EQ(hlp::Param<std::string>::Unpack('a', "dsa"), "");
}