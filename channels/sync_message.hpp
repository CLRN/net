#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"
#include "net/exception.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Owner>
class SyncMessage : public net::details::Channel<typename Owner::Handle,
                                                 typename Owner::Queue,
                                                 typename Owner::Settings>
{
public:
    typedef details::Channel<typename Owner::Handle,
                             typename Owner::Queue,
                             typename Owner::Settings> Base;
    typedef boost::weak_ptr<Owner> OwnerPtr;

    template<typename ... Args>
    SyncMessage(const Args&... args)
        : Base(args...)
        , m_Owner(hlp::Param<OwnerPtr>::Unpack(args...))
        , m_Endpoint(hlp::Param<boost::asio::ip::udp::endpoint>::Unpack(args...))
    {
    }

    //! Begin asynchronous read to buffer
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->async_receive(
            buffer,
            boost::bind(&Base::ReadMessageCallback, Base::shared_from_this(), _1, _2)
        );   
    }

    virtual void Write(const MemHolder& holder) override
    {
        try
        {
            boost::system::error_code e;
            const auto written = Base::m_IoObject->send_to(boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                           m_Endpoint, 0, e);
            if (e)
                BOOST_THROW_EXCEPTION(net::Disconnected("Connection error") << net::SysErrorInfo(e));
            if (written != holder.m_Size)
                BOOST_THROW_EXCEPTION(net::Disconnected("Failed to write data, written: %s, expected: %s",
                                                        written,
                                                        holder.m_Size));
        }
        catch (const std::exception&)
        {
            if (const auto o = m_Owner.lock())
                o->ConnectionClosed(Base::shared_from_this());
        }
    }

    virtual void ConnectionClosed() override
    {
        if (const auto owner = m_Owner.lock())
            owner->ConnectionClosed(Base::shared_from_this());
        Base::m_Callback(IConnection::StreamPtr());
    }

    typename Owner::Handle GetSocket() const
    {
        return Base::m_IoObject;
    }

private:
    const OwnerPtr m_Owner;
    boost::asio::ip::udp::endpoint m_Endpoint;
};

} // namespace channels
} // namespace net
