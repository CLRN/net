#pragma once

#include "conversion/cast.h"
#include "log/log.h"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/factory.hpp"
#include "net/channels/traits.hpp"
#include "net/pipe.hpp"

#include <boost/asio.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <boost/shared_ptr.hpp>
#include <boost/process/pipe.hpp>
#include <boost/process/create_pipe.hpp>
#ifdef WIN32
#include <boost/asio/windows/stream_handle.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif // _DEBUG
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace net
{
namespace streams
{

template
<
    template<typename> class Channel,
    template<typename> class QueueImpl,
    typename Header = details::DefaultHeader,
    typename Settings = DefaultSettings
>
class Transport : public boost::enable_shared_from_this<Transport<Channel, QueueImpl, Header, Settings>>
{
public:
#if defined(BOOST_WINDOWS_API)
    typedef boost::asio::windows::stream_handle Pipe;
#elif defined(BOOST_POSIX_API)
    typedef boost::asio::posix::stream_descriptor Pipe;
#endif

    typedef boost::shared_ptr<Pipe> Handle;
private:
    typedef QueueImpl<Settings> Queue;
    typedef Transport<Channel, QueueImpl, Header, Settings> ThisType;
    typedef boost::enable_shared_from_this<ThisType> Shared;
    typedef net::details::ChannelTraits<Handle, Queue, Settings, ThisType, Header> Traits;
    typedef std::pair<Handle, Handle> Endpoints;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef Channel<Traits> ChannelImpl;
    typedef boost::function<void(const IConnection::Ptr& connection,
                                 const boost::exception_ptr& e)> Callback;

private:
    class StdOutChannel : public ChannelImpl
    {
    public:
        template<typename ... Args>
        StdOutChannel(const Args&... args)
            : ChannelImpl(hlp::Param<Endpoints>::Unpack(args...).first,
                          hlp::Param<Endpoints>::Unpack(args...).second,
                          args...)
        {}

        virtual std::string GetInfo() const override
        {
            return "stdout";
        }
    };

public:
    template<typename ... Args>
    Transport(const Args&... args)
        : m_Factory(details::MakeFactory<StdOutChannel, Endpoints, Ptr>(args..., ::net::details::ByteOrder::Host))
        , m_InAndOut(hlp::Param<Endpoints>::Unpack(args...))
	{
	}

    ~Transport()
	{
        Close();
	}

    void Receive(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        m_ClientConnectedCallback = callback;
        const auto connection = m_Factory->Create(m_InAndOut, Shared::shared_from_this());
        m_ClientConnectedCallback(connection, boost::exception_ptr());
    }

    void ConnectionClosed(const IConnection::Ptr& connection)
    {
        if (m_ClientConnectedCallback)
            m_ClientConnectedCallback(connection, boost::current_exception());
    }

    void Connect(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        throw std::runtime_error("not implemented");
    }

    void Close()
    {
    }

private:

private:
    Callback m_ClientConnectedCallback;
    typename details::IFactory<StdOutChannel, Endpoints, Ptr>::Ptr m_Factory;
    const Endpoints m_InAndOut;
};

template<typename ... Args>
inline boost::process::pipe MakePipe(const Args& ... args)
{
#ifdef WIN32
    boost::system::error_code e;

    net::pipes::Pipe pipe(std::string("\\\\.\\pipe\\") + boost::lexical_cast<std::string>(boost::uuids::random_generator{}()),
                          hlp::Param<boost::asio::io_service>::Unpack(args...),
                          hlp::Param<int>::Unpack(args..., 4096));

    const auto sink = pipe.CreatePipe(e);
    if (e)
        BOOST_THROW_EXCEPTION(net::Exception("Failed to create in pipe") << net::SysErrorInfo(e));

    const auto source = pipe.ConnectToPipe(e);
    if (e)
        BOOST_THROW_EXCEPTION(net::Exception("Failed to create out pipe") << net::SysErrorInfo(e));

    return boost::process::make_pipe(source, sink);
#else
    return boost::process::create_pipe();
#endif // WIN32
}

} // namespace streams
} // namespace net
