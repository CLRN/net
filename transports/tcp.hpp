#pragma once

#include "conversion/cast.h"
#include "log/log.h"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/factory.hpp"

#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace net
{
namespace tcp
{

template
<
    template<typename> class Channel,
    template<typename> class QueueImpl,
    typename SettingsImpl = DefaultSettings
>
class Transport : public boost::enable_shared_from_this<Transport<Channel, QueueImpl, SettingsImpl>>
{
    typedef boost::asio::ip::tcp::socket Socket;
    typedef boost::enable_shared_from_this<Transport<Channel, QueueImpl, SettingsImpl>> Shared;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef boost::shared_ptr<Socket> Handle;
    typedef SettingsImpl Settings;
    typedef QueueImpl<Settings> Queue;
    typedef std::string Endpoint;
    typedef Channel<Transport> ChannelImpl;
    typedef boost::function<void(const typename ChannelImpl::Ptr& connection,
                                 const boost::exception_ptr& e)> Callback;

private:
    class ChannelWithInfoGetter : public ChannelImpl
    {
    public:
        template<typename ... Args>
        ChannelWithInfoGetter(const Args&... args) : ChannelImpl(args...) {}
        virtual std::string GetInfo() override
        {
            return boost::lexical_cast<std::string>(ChannelImpl::m_IoObject->remote_endpoint());
        }
    };

public:
    template<typename ... Args>
    Transport(const Args&... args)
        : m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Acceptor(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Factory(details::MakeFactory<ChannelWithInfoGetter, Handle, Ptr>(args...))
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

        const boost::asio::ip::tcp::endpoint ep(ParseEP(endpoint));
        m_Acceptor.open(ep.protocol());
        m_Acceptor.bind(ep);
        m_Acceptor.listen();

        Accept();       
        Accept();
    }

    void ConnectionClosed(const typename ChannelImpl::Ptr& connection)
    {
        if (m_ClientConnectedCallback)
            m_ClientConnectedCallback(connection, boost::current_exception());

        boost::unique_lock<boost::mutex> lock(m_Mutex);
        const auto socket = static_cast<const ChannelImpl&>(*connection).GetSocket();
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
        const auto socket = boost::make_shared<Socket>(m_Service);
        m_Acceptor.async_accept(*socket,
                                boost::bind(&Transport::ClientAccepted,
                                            Shared::shared_from_this(),
                                            socket,
                                            boost::asio::placeholders::error));

        boost::unique_lock<boost::mutex> lock(m_Mutex);
        m_Sockets.push_back(socket);
    }

    IConnection::Ptr Connect(const std::string& endpoint)
    {
        const auto socket = boost::make_shared<Socket>(m_Service);
        socket->connect(ParseEP(endpoint));
        return m_Factory->Create(socket, Shared::shared_from_this());
    }

    void Close()
    {
        m_Acceptor.close();

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

    boost::asio::ip::tcp::endpoint ParseEP(const std::string& endpoint)
    {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, endpoint, boost::algorithm::is_any_of(":"));
        if (parts.size() != 2)
            BOOST_THROW_EXCEPTION(Exception("Wrong endpoint") << EndpointInfo(endpoint));

        return boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(parts.front()),
                                              conv::cast<short>(parts.back()));
    }

	//! Accept client handler
	void ClientAccepted(const Handle& client, boost::system::error_code e)
	{
		// prepare to accept new client
        Accept();

        if (e)
            return; // failed to accept client

	     // construct new pipe instance
        const auto socket = boost::make_shared<Socket>(m_Service);
        const auto instance = m_Factory->Create(socket, Shared::shared_from_this());
		
	     // invoke callback
	     m_ClientConnectedCallback(instance, boost::exception_ptr());
	}

private:
    boost::asio::io_service& m_Service;
	boost::asio::ip::tcp::acceptor m_Acceptor;
    Callback m_ClientConnectedCallback;
    std::vector<boost::weak_ptr<Socket>> m_Sockets;
    boost::mutex m_Mutex;
    typename details::IFactory<ChannelWithInfoGetter, Handle, Ptr>::Ptr m_Factory;
};

} // namespace tcp
} // namespace net
