#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Traits>
class AsyncStream : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    AsyncStream(const Args&... args)
        : Base(args...)
    {
    }

    virtual void Close() override
    {
        boost::system::error_code e;
        Base::m_IoObject->shutdown(boost::asio::ip::tcp::socket::shutdown_both, e);
        Base::Close();
    }

    //! Begin asynchronous read to buffer
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->async_read_some(
            buffer,
            boost::bind(&Base::ReadMessageCallback, Base::Shared(), _1, _2)
        );   
    }


    virtual void Write(const MemHolder& holder) override
    {
        assert(*reinterpret_cast<const boost::uint32_t*>(holder.m_Memory.get()) ==
               holder.m_Size - sizeof(boost::uint32_t));

        boost::asio::async_write(
                *Base::m_IoObject,
                boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                boost::asio::transfer_exactly(holder.m_Size),
                Base::m_Strand.wrap(boost::bind(&AsyncStream::WriteCallback, Base::Shared(), _1, _2, holder))
        );
    }


};

} // namespace channels
} // namespace net
