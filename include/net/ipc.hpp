#pragma once

#ifdef WIN32

#include "details/transport_impl.hpp"
#include "transports/win32/ipc.hpp"
#include "channels/sync_queue.hpp"

#include <boost/make_shared.hpp>

namespace net
{

typedef net::Transport<net::ipc::Transport, net::channels::SyncQueue, net::details::FakeQueue> Ipc;

namespace ipc
{
    template<typename ... Args>
    static ITransport::Ptr CreateTransport(const Args&... args)
    {
        return boost::make_shared<Ipc>(args...);
    }

} // namespace ipc
} // namespace net

#endif // WIN32
