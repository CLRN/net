#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Traits>
class SyncRandom : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    SyncRandom(const Args&... args)
        : Base(args...)
        , m_WriteOffset()
        , m_ReadOffset()
    {
    }

    //! Begin asynchronous read to buffer
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        boost::asio::async_read_at
        (
            *Base::m_IoObject,
            m_ReadOffset,
            buffer,
            Base::m_Strand.wrap(boost::bind(&Base::ReadMessageCallback, Base::Shared(), _1, _2, Base::m_IoObject))
        );     
    }

    virtual void Write(const typename Base::MemHolder& holder) override
    {
        boost::system::error_code e;
        const auto written = boost::asio::write_at(*Base::m_IoObject,
                                                   m_WriteOffset,
                                                   boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                   boost::asio::transfer_all(), e);
        Base::WriteCallback(e, written, holder, Base::m_IoObject);
        m_WriteOffset += written;
    }

    virtual void OnBytesRead(const std::size_t bytes) override
    {
        m_ReadOffset += bytes;
    }

private:
    boost::uint64_t m_ReadOffset;
    boost::uint64_t m_WriteOffset;
};

} // namespace channels
} // namespace net
