#include "net/details/params.hpp"

#include <gtest/gtest.h>

TEST(Packer, Unpack)
{
    int var = 1;
    int t = hlp::Param<const int>::Unpack('d', "dsdsa", var, 1);
    EXPECT_EQ(t, var);
    hlp::Param<int>::Unpack(var) = 10;
    EXPECT_EQ(10, var);

    EXPECT_EQ(hlp::Param<const int>::Unpack('a', "dsa", 100), 100);
    EXPECT_EQ(hlp::Param<const std::string>::Unpack('a', "dsa", std::string("default")), "default");
    EXPECT_EQ(hlp::Param<int>::Unpack(std::make_tuple("s", 1, false)), 1);
}

TEST(Packer, Refs)
{
    int t = 0;
    std::string s;
    int& ref1 = hlp::Param<int>::Unpack(std::string(), t);
    std::string& ref2 = hlp::Param<std::string>::Unpack(s, t);
    std::string& ref3 = hlp::Param<std::string>::Unpack(int(), s);

    std::string& ref4 = hlp::Param<std::string>::Unpack(int(), std::ref(s));

    ref4 = "test";
    EXPECT_EQ(ref4, s);
}
