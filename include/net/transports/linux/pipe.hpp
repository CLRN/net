#pragma once

#include "conversion/cast.h"
#include "log/log.h"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/factory.hpp"
#include "net/channels/traits.hpp"
#include "net/details/channel.hpp"

#include <boost/asio.hpp>
//#include <boost/asio/local/stream_protocol.hpp>
//#include <boost/algorithm/string/split.hpp>
//#include <boost/algorithm/string/classification.hpp>
//#include <boost/range/algorithm.hpp>
//#include <boost/bind.hpp>
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
    typename Settings = DefaultSettings
>
class Transport : public boost::enable_shared_from_this<Transport<Channel, QueueImpl, Settings>>
{
    typedef boost::asio::local::stream_protocol::socket Socket;
    typedef boost::shared_ptr<Socket> Handle;
    typedef QueueImpl<Settings> Queue;
    typedef Transport<Channel, QueueImpl, Settings> ThisType;
    typedef boost::enable_shared_from_this<ThisType> Shared;
    typedef net::details::ChannelTraits<Handle, Queue, Settings, ThisType> Traits;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef Channel<Traits> ChannelImpl;

private:
    class DomainSocketChannel : public ChannelImpl
    {
    public:
        template<typename ... Args>
        DomainSocketChannel(const Args&... args) : ChannelImpl(args...) {}

        virtual std::string GetInfo() const override
        {
            return boost::lexical_cast<std::string>(ChannelImpl::m_IoObject->remote_endpoint());
        }

        virtual void Close() override
        {
            boost::system::error_code e;
            ChannelImpl::m_IoObject->shutdown(boost::asio::local::stream_protocol::socket::shutdown_both, e);
            ChannelImpl::Close();
        }
    };

public:
    template<typename ... Args>
    Transport(const Args&... args)
        : m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Acceptor(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Factory(details::MakeFactory<DomainSocketChannel, Handle, Ptr>(args..., ::net::details::ByteOrder::Host))
	  {}

    ~Transport()
	  {
        Close();
	  }

    void Receive(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        m_ClientConnectedCallback = callback;

        const boost::asio::local::stream_protocol::endpoint ep(MakeValidName(endpoint));

        if (boost::filesystem::exists(ep.path()))
            boost::filesystem::remove(ep.path());

        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            m_ServerSocketsPath.emplace(ep.path());
        }

        boost::system::error_code ignore;
        m_Acceptor.close(ignore);

        m_Acceptor.open(ep.protocol());
        m_Acceptor.bind(ep);
        m_Acceptor.listen();

        Accept();
    }

    void ConnectionClosed(const IConnection::Ptr& connection)
    {
        if (m_ClientConnectedCallback)
            m_ClientConnectedCallback(connection, boost::current_exception());

        boost::unique_lock<boost::mutex> lock(m_Mutex);
        const auto socket = static_cast<const ChannelImpl&>(*connection).GetHandle();
        const auto it = boost::find_if(m_Sockets, [&socket](const boost::weak_ptr<Socket>& s){
            if (const auto locked = s.lock())
                return locked == socket;
            return false;
        });

        if (it != m_Sockets.end())
            m_Sockets.erase(it);
    }

    void Accept()
    {
        if (!m_Acceptor.is_open())
            return;

        const auto socket = boost::make_shared<Socket>(m_Service);
        m_Acceptor.async_accept(*socket,
                                boost::bind(&Transport::ClientAccepted,
                                            Shared::shared_from_this(),
                                            socket,
                                            boost::asio::placeholders::error));

        boost::unique_lock<boost::mutex> lock(m_Mutex);
        m_Sockets.push_back(socket);
    }

    void Connect(const std::string& endpoint, const ITransport::Callback& callback)
    {
        const boost::weak_ptr<ThisType> instance(Shared::shared_from_this());
        m_Service.post([callback, endpoint, this, instance]()
        {
            const auto socket = boost::make_shared<Socket>(m_Service);

            try
            {
                socket->connect(MakeValidName(endpoint));
                const auto ep = m_Factory->Create(socket, Shared::shared_from_this());
                callback(ep, boost::exception_ptr());
            }
            catch (const std::exception&)
            {
                callback(IConnection::Ptr(), boost::current_exception());
            }
        });
    }

    void Close()
    {
        m_Acceptor.close();

        std::vector<boost::weak_ptr<Socket>> copy;
        std::set<std::string> paths;
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            copy.swap(m_Sockets);
            paths.swap(m_ServerSocketsPath);
        }

        boost::system::error_code e;
        for (const auto& socket : copy)
        {
            if (const auto s = socket.lock())
            {
                s->shutdown(boost::asio::ip::tcp::socket::shutdown_both, e);
                s->close(e);
            }
        }

        for (const auto& path : paths)
        {
            if (boost::filesystem::exists(path))
                boost::filesystem::remove(path);
        }
    }

private:
    std::string MakeValidName(const std::string& name)
    {
        static const std::string prefix = "/tmp/";
        if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix)
            return name;

        return prefix + name;
    }

    //! Accept client handler
    void ClientAccepted(const Handle& client, boost::system::error_code e)
    {
        // prepare to accept new client
        Accept();

        if (e)
            return; // failed to accept client

        // construct new channel instance
        const auto instance = m_Factory->Create(client, Shared::shared_from_this());

        // invoke callback
        m_ClientConnectedCallback(instance, boost::exception_ptr());
    }

private:
    boost::asio::io_service& m_Service;
    boost::asio::local::stream_protocol::acceptor m_Acceptor;
    ITransport::Callback m_ClientConnectedCallback;
    std::vector<boost::weak_ptr<Socket>> m_Sockets;
    boost::mutex m_Mutex;
    std::set<std::string> m_ServerSocketsPath;
    typename details::IFactory<DomainSocketChannel, Handle, Ptr>::Ptr m_Factory;
};

} // namespace pipe
} // namespace net
