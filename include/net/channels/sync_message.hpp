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
    virtual void DoRead(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->async_receive(
            buffer,
            Base::m_Strand.wrap(boost::bind(&Base::ReadMessageCallback, Base::Shared(), _1, _2, Base::m_IoObject))
        );   
    }

    virtual void DoWrite(const std::vector<boost::asio::const_buffer>& buffers) override
    {
        boost::system::error_code e;
        const auto written = Base::m_IoObject->send_to(buffers, m_Endpoint, 0, e);
        Base::WriteCallback(written, e, Base::m_IoObject);
    }

private:
    boost::asio::ip::udp::endpoint m_Endpoint;
};

} // namespace channels
} // namespace net
