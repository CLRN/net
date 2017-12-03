#pragma once

#include "details/transport_impl.hpp"
#include "transports/tcp.hpp"
#include "channels/async_stream.hpp"

#include <boost/make_shared.hpp>

namespace net
{

typedef net::Transport<net::tcp::Transport, net::channels::AsyncStream> Tcp;
typedef net::Transport<net::tcp::Transport, 
                       net::channels::AsyncStream, 
                       net::details::PersistentQueue, 
                       net::PersistentSettings> PersistentTcp;

namespace tcp
{
    template<typename ... Args>
    static ITransport::Ptr CreateTransport(const Args&... args)
    {
        return boost::make_shared<Tcp>(args...);
    }

    template<typename ... Args>
    static ITransport::Ptr CreatePersistentTransport(const Args&... args)
    {
        return boost::make_shared<PersistentTcp>(args...);
    }

} // namespace tcp
} // namespace net