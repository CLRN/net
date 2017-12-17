#pragma once

#include "net/transport.hpp"
#include "net/connection.hpp"
#include "settings.hpp"
#include "persistent_queue.hpp"
#include "channel.hpp"

#include <boost/asio/io_service.hpp>

namespace net
{

template
<
    template
    <
        template<typename> class Channel,
        template<typename> class Queue,
        typename Header,
        typename Settings
    > class TransportImpl,
    template<typename> class Channel,
    template<typename> class Queue = details::PersistentQueue,
    typename Header = details::DefaultHeader,
    typename Settings = DefaultSettings
>
class Transport : public net::ITransport
{
public:
    typedef TransportImpl<Channel, Queue, Header, Settings> Impl;
    typedef IConnection::StreamPtr Stream;

    template<typename ...Args>
    Transport(const Args&... args)
        : m_Impl(boost::make_shared<Impl>(args...))
    {
    }

    //! Stop all activity
    virtual void Close() override
    {
        m_Impl->Close();
    }

    //! Connect to remote host
    virtual void Connect(const Endpoint& endpoint, const Callback& callback) override
    {
        m_Impl->Connect(endpoint, callback);
    }

    //! Receive callback
    virtual void Receive(const Endpoint& endpoint, const Callback& callback) override
    {
        m_Impl->Receive(endpoint, callback);
    }

private:
    const boost::shared_ptr<Impl> m_Impl;
};

} // namespace net