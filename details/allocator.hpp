#pragma once

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

namespace net
{

class CrtAllocator
{
public:
    typedef boost::shared_ptr<char[]> Memory;

    struct MemHolder
    {
        Memory m_Memory;
        std::size_t m_Size;
    };

    static Memory Allocate(std::size_t size)
    {
        return boost::make_shared_noinit<char[]>(size);
    }
};

} // namespace net

