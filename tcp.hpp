#pragma once

#include "details/transport.hpp"
#include "transports/tcp.hpp"
#include "channels/sync_stream.hpp"

namespace net
{
    typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> Tcp;
} // namespace net