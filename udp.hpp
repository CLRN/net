#pragma once

#include "details/transport.hpp"
#include "transports/udp.hpp"
#include "channels/sync_message.hpp"

namespace net
{
    typedef net::Transport<net::udp::Transport, net::channels::SyncMessage> Udp;
} // namespace net