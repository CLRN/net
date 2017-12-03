#pragma once

#include "details/transport_impl.hpp"

#ifdef WIN32
#include "transports/win32/pipe.hpp"
#else
#include "transports/linux/pipe.hpp"
#endif // WIN32
#include "channels/async_stream.hpp"

#include <boost/make_shared.hpp>

namespace net
{
    typedef net::Transport<net::pipe::Transport, net::channels::AsyncStream> Pipe;

namespace pipe
{
    template<typename ... Args>
    static ITransport::Ptr CreateTransport(const Args&... args)
    {
        return boost::make_shared<Pipe>(args...);
    }

} // namespace pipe
} // namespace net