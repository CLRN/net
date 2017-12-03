#pragma once

#include "details/transport_impl.hpp"
#include "transports/std.hpp"
#include "channels/std_stream.hpp"

#include <boost/make_shared.hpp>

namespace net
{
typedef net::Transport<net::streams::Transport, net::channels::StdStream> Std;

namespace streams
{
    template<typename ... Args>
    static ITransport::Ptr CreateTransport(const Args&... args)
    {
        return boost::make_shared<Std>(args...);
    }

} // namespace streams
} // namespace net