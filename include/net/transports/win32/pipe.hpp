#pragma once

#include "conversion/cast.h"
#include "log/log.h"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/factory.hpp"
#include "net/channels/traits.hpp"
#include "net/transports/win32/async_pipe.hpp"

#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace net
{
namespace pipe
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
    typedef pipes::Pipe::Ptr Handle;
    typedef QueueImpl<Settings> Queue;
    typedef Transport<Channel, QueueImpl, Header, Settings> ThisType;
    typedef boost::enable_shared_from_this<ThisType> Shared;
    typedef net::details::ChannelTraits<Handle, Queue, Settings, ThisType, Header> Traits;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef Channel<Traits> ChannelImpl;
    typedef boost::function<void(const IConnection::Ptr& connection,
                                 const boost::exception_ptr& e)> Callback;

private:
    class PipeChannel : public ChannelImpl
    {
    public:
        template<typename ... Args>
        PipeChannel(const Args&... args) 
            : ChannelImpl(args...)
            , m_PipeName(hlp::Param<std::string>::Unpack(args...))
        {}

        virtual std::string GetInfo() const override
        {
            return m_PipeName;
        }

    private:
        const std::string m_PipeName;
    };

public:
    template<typename ... Args>
    Transport(const Args&... args)
        : m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Factory(details::MakeFactory<PipeChannel, Handle, std::string, Ptr>(args..., ::net::details::ByteOrder::Host))
        , m_BufferSize(4096)
        , m_StartupInstances(hlp::Param<int>::Unpack(args..., int(5)))
    {
    }

    ~Transport()
    {
        Close();
    }

    void Receive(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        m_ClientConnectedCallback = callback;
        const auto ep = MakeValidName(endpoint);
        m_Pipe = boost::make_shared<pipes::Pipe>(ep, m_Service, m_BufferSize);
        for (std::size_t i = 0; i < m_StartupInstances; ++i)
            m_Pipe->AsyncAccept(boost::bind(&ThisType::ClientAccepted, shared_from_this(), _1, _2, ep));
    }

    void ConnectionClosed(const IConnection::Ptr& connection)
    {
        if (m_ClientConnectedCallback)
            m_ClientConnectedCallback(connection, boost::current_exception());
    }

    void Connect(const std::string& endpoint, const ITransport::Callback& callback)
    {
        const boost::weak_ptr<ThisType> instance(Shared::shared_from_this());
        m_Service.post([callback, endpoint, this, instance]()
        {
            if (const auto locked = instance.lock())
            {
                pipes::Pipe pipe(MakeValidName(endpoint), m_Service, m_BufferSize);

                pipes::Pipe::Ptr handle;
                pipe.Connect(handle);
                const auto ep = m_Factory->Create(handle, endpoint, locked);
                callback(ep, boost::exception_ptr());
            }
        });
    }

    void Close()
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        if (m_Pipe)
            m_Pipe->Close();
        m_Pipe.reset();
    }

private:

    std::string MakeValidName(const std::string& name)
    {
        static const std::string prefix = "\\\\.\\pipe\\";
        if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix)
            return name;

        return prefix + name;
    }

    //! Accept client handler
    void ClientAccepted(const Handle& client, boost::system::error_code e, const std::string& ep)
    {
        // prepare to accept new client
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            if (m_Pipe)
                m_Pipe->AsyncAccept(boost::bind(&ThisType::ClientAccepted, shared_from_this(), _1, _2, ep));
        }

        if (e)
            return; // failed to accept client

        // construct new channel instance
        const auto instance = m_Factory->Create(client, ep, Shared::shared_from_this());
        
         // invoke callback
         m_ClientConnectedCallback(instance, boost::exception_ptr());
    }

private:
    const std::size_t m_BufferSize;
    const std::size_t m_StartupInstances;
    boost::asio::io_service& m_Service;
    boost::shared_ptr<pipes::Pipe> m_Pipe;
    Callback m_ClientConnectedCallback;
    boost::mutex m_Mutex;
    typename details::IFactory<PipeChannel, Handle, std::string, Ptr>::Ptr m_Factory;
};

} // namespace pipe
} // namespace net
