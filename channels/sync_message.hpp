#pragma once

#include "net/details/channel_base.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Owner>
class SyncMessage : public net::details::Channel<typename Owner::Handle,
                                                 typename Owner::Allocator,
                                                 typename Owner::Queue,
                                                 typename Owner::Settings>
{
public:
    typedef typename Owner::Allocator::MemHolder MemHolder;
    typedef details::Channel<typename Owner::Handle,
                             typename Owner::Allocator,
                             typename Owner::Queue,
                             typename Owner::Settings> Base;
    typedef boost::weak_ptr<Owner> OwnerPtr;

    SyncMessage(boost::asio::io_service& svc,
                const boost::asio::ip::udp::endpoint& ep,
                const OwnerPtr& owner,
                const typename Owner::Handle& handle)
        : Base(svc, handle)
        , m_Owner(owner)
        , m_Endpoint(ep)
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
        boost::system::error_code e;
        const auto written = Base::m_IoObject->send_to(boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                       m_Endpoint);
        if (written != holder.m_Size || e)
        {
            if (const auto o = m_Owner.lock())
                o->ConnectionClosed(Base::shared_from_this(), e);
        }
    }

    virtual void ConnectionClosed(const boost::system::error_code& e, std::size_t bytes) override
    {
        if (const auto owner = m_Owner.lock())
        {
            LOG_INFO("Connection closed, bytes: %s, error: (%s) %s", bytes, e.value(), e.message());
            owner->ConnectionClosed(Base::shared_from_this(), e);
        }
        Base::m_Callback(typename IConnection<typename Owner::Allocator>::StreamPtr());
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
