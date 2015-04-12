#pragma once

#include <boost/shared_ptr.hpp>

namespace net
{

typedef boost::shared_ptr<char[]> Memory;

struct MemHolder
{
    Memory m_Memory;
    std::size_t m_Size;
};

} // namespace net

