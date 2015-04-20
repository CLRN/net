#include "net/details/params.hpp"

#include <gtest/gtest.h>

TEST(Packer, Unpack)
{
    int var = 1;
    int t = hlp::Param<const int>::Unpack('d', "dsdsa", var, 1);
    EXPECT_EQ(t, var);
    hlp::Param<int>::Unpack(std::ref(var)) = 10;
    EXPECT_EQ(10, var);
}

TEST(Packer, Refs)
{
    int t = 0;
    std::string s;
    int& ref1 = hlp::Param<int>::Unpack(std::string(), std::ref(t));
    std::string& ref2 = hlp::Param<std::string>::Unpack(std::ref(s), std::ref(t));
    std::string& ref3 = hlp::Param<std::string>::Unpack(int(), std::ref(s));
}

TEST(Packer, Default)
{
    EXPECT_EQ(hlp::Param<const int>::Unpack('a', "dsa", 100), 1000);
    EXPECT_EQ(hlp::Param<const std::string>::Unpack('a', "dsa", std::string("default")), "default");
}