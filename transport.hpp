#pragma once

#include "connection.hpp"
#include "settings.hpp"

#include <boost/asio/io_service.hpp>

namespace net
{

template
<
    template<template<typename> class, typename, typename> class TransportImpl,
    template<typename> class Channel,
    typename Settings = DefaultSettings,
    typename A = CrtAllocator
>
class Transport
{
public:
    typedef A Allocator;
    typedef TransportImpl<Channel, Allocator, Settings> Impl;
    typedef typename Impl::Endpoint Endpoint;
    typedef IConnection<Allocator> Connection;
    typedef typename Connection::StreamPtr Stream;

    Transport(boost::asio::io_service& svc)
        : m_Impl(boost::make_shared<Impl>(svc))
    {

    }

    //! Connect to remote host
    typename Connection::Ptr Connect(const Endpoint& endpoint)
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