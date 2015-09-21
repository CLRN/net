#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"
#include "net/exception.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Traits>
class SyncMessage : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    SyncMessage(const Args&... args)
        : Base(args...)
        , m_Endpoint(hlp::Param<boost::asio::ip::udp::endpoint>::Unpack(args...))
    {
    }

    //! Begin asynchronous read to buffer
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->async_receive(
            buffer,
            boost::bind(&Base::ReadMessageCallback, Base::Shared(), _1, _2)
        );   
    }

    virtual void Write(const MemHolder& holder) override
    {
        boost::system::error_code e;
        const auto written = Base::m_IoObject->send_to(boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                       m_Endpoint, 0, e);
        Base::WriteCallback(e, written, holder);
    }

private:
    boost::asio::ip::udp::endpoint m_Endpoint;
};

} // namespace channels
} // namespace net
