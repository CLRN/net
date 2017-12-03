#pragma once

#include <iostream>

#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/categories.hpp>
#include <boost/shared_ptr.hpp>

namespace net
{
namespace details
{

template<typename Container>
class SeekableStreamBuf
{
public:
    typedef char char_type;
    typedef boost::iostreams::seekable_device_tag category;

    SeekableStreamBuf(Container* data) : m_Position()
    {
        m_Data.swap(*data);
    }

    std::streamsize read(char_type* s, std::streamsize n)
    {
        if (m_Position >= static_cast<std::streamoff>(m_Data.size()))
            return 0;

        const auto pos = static_cast<std::size_t>(m_Position);
        const auto remaining = static_cast<std::streamoff>(m_Data.size() - pos);
        const auto toRead = n > remaining ? remaining : n;
        std::copy(m_Data.begin() + pos, m_Data.begin() + pos + static_cast<std::size_t>(toRead), s);
        m_Position += toRead;
        return toRead;
    }

    std::streamsize write(const char_type* /*s*/, std::streamsize /*n*/)
    {
        assert(!"not implemented");
        return 0;
    }

    std::streampos seek(std::streamoff offset, std::ios::seekdir dir)
    {
        if (dir == std::ios::cur)
            m_Position += offset;
        else
        if (dir == std::ios::beg)
            m_Position = offset;
        else
            m_Position = m_Data.size() - offset;

        return m_Position;
    }

private:
    Container m_Data;
    std::streamoff m_Position;
};

} // namespace details
} // namespace net

