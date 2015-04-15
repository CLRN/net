#pragma once

#include "connection.hpp"
#include "settings.hpp"
#include "persistent_queue.hpp"

#include <boost/asio/io_service.hpp>

namespace net
{

template
<
    template<
        template<typename> class Channel,
        template<typename> class Queue,
        typename> class TransportImpl,
    template<typename> class Channel,
    template<typename> class Queue = details::PersistentQueue,
    typename Settings = DefaultSettings
>
class Transport
{
public:
    typedef TransportImpl<Channel, Queue, Settings> Impl;
    typedef typename Impl::Endpoint Endpoint;
    typedef IConnection::StreamPtr Stream;

    template<typename ...Args>
    Transport(const Args&... args)
        : m_Impl(boost::make_shared<Impl>(args...))
    {

    }

    //! Connect to remote host
    IConnection::Ptr Connect(const Endpoint& endpoint)
    {
        return m_Impl->Connect(endpoint);
    }

    //! Receive callback
    template<typename Callback>
    void Receive(const Endpoint& endpoint, const Callback& callback)
    {
        return m_Impl->Receive(endpoint, callback);
    }

    //! Stop all activity
    void Close()
    {
        m_Impl.Close();
    }

private:
    boost::shared_ptr<Impl> m_Impl;
};

} // namespace net