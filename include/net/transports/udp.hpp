#pragma once

#include "conversion/cast.hpp"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"

#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace net
{
namespace udp
{

template
<
    template<typename> class Channel,
    template<typename> class QueueImpl,
    typename Header = details::DefaultHeader,
    typename SettingsImpl = DefaultSettings
>
class Transport : public boost::enable_shared_from_this<Transport<Channel, QueueImpl, SettingsImpl>>
{
    typedef boost::asio::ip::udp::socket Socket;
    typedef boost::enable_shared_from_this<Transport<Channel, QueueImpl, SettingsImpl>> Shared;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef SettingsImpl Settings;
    typedef QueueImpl<Settings> Queue;
    typedef boost::shared_ptr<Socket> Handle;
    typedef std::string Endpoint;
    typedef Transport<Channel, QueueImpl, Header, Settings> ThisType;
    typedef net::details::ChannelTraits<Handle, Queue, Settings, ThisType, Header> Traits;
    typedef Channel<Traits> ChannelImpl;
    typedef boost::function<void(const IConnection::Ptr& connection,
                                 const boost::exception_ptr& e)> Callback;

    class UDPChannel : public ChannelImpl
    {
    public:
        template<typename ... Args>
        UDPChannel(const Args&... args) : ChannelImpl(args...) {}

        virtual std::string GetInfo() const override
        {
            return boost::lexical_cast<std::string>(ChannelImpl::m_IoObject->remote_endpoint());
        }
    };

    template<typename ... Args>
    Transport(const Args&... args)
        : m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Factory(details::MakeFactory<UDPChannel, Handle, Ptr>(args...))
	{
	}

    ~Transport()
	{
        Close();
	}

    template<typename T>
    void Receive(const Endpoint& endpoint, const T& callback)
    {
        m_ClientConnectedCallback = callback;

        const boost::asio::ip::udp::endpoint ep(ParseEP(endpoint));

        const auto socket = boost::make_shared<Socket>(m_Service, ep);
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            m_Sockets.push_back(socket);
        }

        const auto connection = m_Factory->Create(socket);
        m_ClientConnectedCallback(connection, boost::system::error_code());
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

    IConnection::Ptr Connect(const std::string& endpoint)
    {
        const auto socket = boost::make_shared<Socket>(m_Service, boost::asio::ip::udp::v4());
        
        const auto ep = ParseEP(endpoint);
        socket->connect(ep);
        return m_Factory->Create(socket);
    }

    void Close()
    {
        std::vector<boost::weak_ptr<Socket>> copy;
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            copy.swap(m_Sockets);
        }

        boost::system::error_code e;
        for (const auto& socket : copy)
        {
            if (const auto s = socket.lock())
                s->close(e);
        }
    }

private:

    boost::asio::ip::udp::endpoint ParseEP(const std::string& endpoint)
    {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, endpoint, boost::algorithm::is_any_of(":"));
        if (parts.size() != 2)
            BOOST_THROW_EXCEPTION(Exception("Wrong endpoint: %s", endpoint));

        return boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(parts.front()),
                                              conv::cast<short>(parts.back()));
    }

private:
    boost::asio::io_service& m_Service;
    Callback m_ClientConnectedCallback;
    std::vector<boost::weak_ptr<Socket>> m_Sockets;
    boost::mutex m_Mutex;
    typename details::IFactory<UDPChannel, Handle>::Ptr m_Factory;
};

} // namespace udp
} // namespace net
