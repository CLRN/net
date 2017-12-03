#pragma once

#include "conversion/cast.hpp"
#include "log/log.h"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/factory.hpp"
#include "net/channels/traits.hpp"
#include "net/details/channel.hpp"
#include "net/transport.hpp"

#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/future.hpp>

#ifdef _WIN32

#include <mstcpip.h>
#include <winsock2.h>

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>


#endif  // _WIN32


namespace net
{
namespace tcp
{

enum class KeepAlive
{
    Disabled = 0,
    Enabled = 1
};

enum class HandleInheritance
{
    InheritHandle = 0,
    NoInheritHandle = 1
};

struct TimeoutSettings
{
    boost::posix_time::time_duration m_ConnectionTimeout;
    boost::posix_time::time_duration m_ReconnectionTimeout;
};

template
<
    template<typename> class Channel,
    template<typename> class QueueImpl,
    typename Settings = DefaultSettings
>
class Transport : public boost::enable_shared_from_this<Transport<Channel, QueueImpl, Settings>>
{
    typedef boost::asio::ip::tcp::socket Socket;
    typedef boost::shared_ptr<Socket> Handle;
    typedef QueueImpl<Settings> Queue;
    typedef Transport<Channel, QueueImpl, Settings> ThisType;
    typedef boost::enable_shared_from_this<ThisType> Shared;
    typedef net::details::ChannelTraits<Handle, Queue, Settings, ThisType> Traits;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef Channel<Traits> ChannelImpl;

private:
    class TCPChannel : public ChannelImpl
    {
    public:
        typedef boost::shared_ptr<TCPChannel> Ptr;

        template<typename ... Args>
        TCPChannel(const Args&... args) 
            : ChannelImpl(args...) 
            , m_Endpoint(ChannelImpl::m_IoObject->remote_endpoint())
        {}

        virtual std::string GetInfo() const override
        {
            return boost::lexical_cast<std::string>(m_Endpoint);
        }

        virtual void Close() override
        {
            boost::system::error_code e;
            ChannelImpl::m_IoObject->shutdown(boost::asio::ip::tcp::socket::shutdown_both, e);
            ChannelImpl::Close();
        }

        void Terminate()
        {
            ChannelImpl::m_IsTerminating = true;
            Close();
        }

        void SetHandle(const typename Traits::Handle& handle)
        {
            Close();
            ChannelImpl::m_IoObject = handle;
            m_Endpoint = handle->remote_endpoint();
        }

        boost::asio::ip::tcp::endpoint GetRemoteEndpoint() const
        {
            return m_Endpoint;
        }

        void TimeoutExpired()
        {
            try
            {
                ChannelImpl::m_Queue.Clear();
                BOOST_THROW_EXCEPTION(net::Disconnected("Timeout has expired"));
            }
            catch (const std::exception&)
            {
                ChannelImpl::m_Callback(IConnection::StreamPtr());
            }
        }

        void Synchronize(const Handle& socket)
        {
            boost::system::error_code ignore;

            // close previous connection immediately
            ChannelImpl::m_IoObject->close(ignore);

            SendSyncPacket(socket);

            const auto ep = boost::lexical_cast<std::string>(socket->remote_endpoint());
            LOG_INFO("Reading sync packet from: %s", ep);

            // read to special handler
            const auto header = boost::make_shared<net::details::RestorationHeader>();
            static const auto headerSize = sizeof(net::details::RestorationHeader);
            const auto instance = ChannelImpl::Shared();
            const auto timer = boost::make_shared<boost::asio::deadline_timer>(socket->get_io_service());
            const auto condition = boost::make_shared<std::atomic<bool>>(false);
            const auto owner = ChannelImpl::m_Owner;
            const auto direction = ChannelImpl::m_Direction;

            timer->expires_from_now(boost::posix_time::minutes(1));
            timer->async_wait([timer, socket, this, instance, condition, owner, direction](boost::system::error_code e)
            {
                if (e)
                    return;

                if (const auto lock = owner.lock())
                {
                    const auto ep = socket->remote_endpoint();
                    LOG_ERROR("Failed to synchronize within 1 minute, ep: %s", ep);

                    if (direction == details::Direction::Outgoing)
                    {
                        lock->ConnectWithTimer(m_ConnectToServerCallback, ep, boost::dynamic_pointer_cast<TCPChannel>(instance));
                        *condition = true;
                    }
                    socket->close(e);
                }
            });

            boost::asio::async_read(*socket,
                boost::asio::buffer(header.get(), headerSize),
                boost::asio::transfer_exactly(headerSize),
                [header, socket, instance, this, ep, timer, condition, owner](boost::system::error_code e, std::size_t bytes)
            {
                try
                {
                    if (*condition)
                        return;

                    boost::system::error_code ignore;
                    timer->cancel(ignore);
                    LOG_INFO("Received sync packet from: %s, size: %s, error: %s", ep, bytes, (e ? e.message() : "no"));

                    SetHandle(socket);
                    this->HandleRestorationHeader(*header, e);
                }
                catch (const std::exception&)
                {
                    LOG_ERROR("Failed to synchronize, error: %s", boost::current_exception_diagnostic_information());
                    socket->close(e);
                    if (const auto locked = owner.lock())
                        locked->ConnectionClosed(instance);
                }
            });

        }

        void SendSyncPacket(const Handle& socket)
        {
            // make a reconnection packet
            const auto buffer = ChannelImpl::RestoreChannel();

            LOG_INFO("Writing sync packet to: %s, size: %s", boost::lexical_cast<std::string>(socket->remote_endpoint()), buffer.size());

            const auto size = boost::asio::write(*socket, boost::asio::buffer(buffer));
            assert(size == buffer.size());
        }

        void PutBackHeader(const net::details::RestorationHeader& header)
        {
            this->PrepareBuffer();
            assert(!ChannelImpl::m_ReadBytes);

            const auto bytes = sizeof(net::details::RestorationHeader);

            memcpy(&ChannelImpl::m_ReadBuffer[ChannelImpl::m_ReadBytes], &header, bytes);
            ChannelImpl::OnBytesRead(bytes);

            // commit received bytes to the input sequence
            ChannelImpl::m_ReadBytes += bytes;
            ChannelImpl::m_ReceivedBytesLocal += bytes;
        }

        void SetConnectToServerCallback(const ITransport::Callback& cb)
        {
            m_ConnectToServerCallback = cb;
        }

        const ITransport::Callback& GetConnectToServerCallback() const
        {
            return m_ConnectToServerCallback;
        }

    private:
        boost::asio::ip::tcp::endpoint m_Endpoint;
        ITransport::Callback m_ConnectToServerCallback;
    };

public:
    template<typename ... Args>
    Transport(const Args&... args)
        : m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Acceptor(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_AcceptStrand(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Factory(details::MakeFactory<TCPChannel, Handle, Ptr, details::Direction>(args...))
        , m_KeepAlive(hlp::Param<KeepAlive>::Unpack(args..., KeepAlive::Enabled))
        , m_HandleInheritance(hlp::Param<HandleInheritance>::Unpack(args..., HandleInheritance::InheritHandle))
        , m_IsClosed()
        , m_TimeoutSettings(hlp::Param<TimeoutSettings>::Unpack(args..., TimeoutSettings()))
    {
    }

    ~Transport()
    {
        Close();
    }

    template<typename SocketHandleWrapper>
    void ConfigureInheritance(SocketHandleWrapper& wrapper)
    {
#ifdef _WIN32
        if (m_HandleInheritance == HandleInheritance::NoInheritHandle)
        {
            auto winSocket = wrapper.native_handle();
            auto winSocketAsHandle = reinterpret_cast<HANDLE>(static_cast<size_t>(winSocket));
            SetHandleInformation(winSocketAsHandle, HANDLE_FLAG_INHERIT, 0);
        }
#endif //TODO: for linux: fcntl(socket->native_handle(), F_SETFD, FD_CLOEXEC);
    }

    void Receive(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        m_ClientConnectedCallback = callback;

        auto parsedEP = ParseEP(endpoint);
        if (IsIP(parsedEP.first))
        {
            const boost::asio::ip::tcp::endpoint ep(boost::asio::ip::address::from_string(parsedEP.first), parsedEP.second);
            ReceiveByIP(ep);
        }
        else
        {
            boost::asio::ip::tcp::resolver resolver(m_Service);
            const boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), parsedEP.first, std::to_string(parsedEP.second));
            const boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);
            const boost::asio::ip::tcp::endpoint ep(iterator->endpoint().address(), parsedEP.second);
            ReceiveByIP(ep);
        }
    }

    void ReceiveByIP(const boost::asio::ip::tcp::endpoint& ep)
    {
        if (m_Acceptor.is_open())
            m_Acceptor.close();
        m_Acceptor.open(ep.protocol());
#ifdef _WIN32
        int exclusiveReuse{ 1 };
        const int result{ setsockopt(m_Acceptor.native_handle(),
                                     SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                                     reinterpret_cast<char*>(&exclusiveReuse),
                                     sizeof exclusiveReuse) };
        if (result != 0)
            BOOST_THROW_EXCEPTION(net::Exception("Failed to set SO_EXCLUSIVEADDRUSE on socket.")
                << net::SysErrorInfo(boost::system::error_code(WSAGetLastError(), boost::system::system_category())));
#else
        m_Acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
#endif  // _WIN32
        m_Acceptor.bind(ep);
        m_Acceptor.listen();
        ConfigureInheritance(m_Acceptor); 

        Accept();
        Accept();
    }

    void ConnectionClosed(const IConnection::Ptr& connection)
    {
        const auto channel = boost::dynamic_pointer_cast<TCPChannel>(connection);

        LOG_DEBUG("Connection %s %s has terminated", (channel->GetDirection() == details::Direction::Incoming ? "from" : "to"), connection->GetInfo());

        if (channel->GetDirection() == details::Direction::Outgoing && channel->GetConnectToServerCallback())
            channel->GetConnectToServerCallback()(connection, boost::current_exception());

        if (channel->GetDirection() == details::Direction::Incoming && m_ClientConnectedCallback)
            m_ClientConnectedCallback(connection, boost::current_exception());

        const auto socket = static_cast<const ChannelImpl&>(*connection).GetHandle();

        boost::unique_lock<boost::mutex> lock(m_Mutex);
        {
            const auto it = boost::find_if(m_Sockets, [&socket](const boost::weak_ptr<Socket>& s){
                if (const auto locked = s.lock())
                    return locked == socket;
                return false;
            });

            if (it != m_Sockets.end())
                m_Sockets.erase(it);
        }

        if (m_IsClosed || !Traits::Settings::IsPersistent() || channel->IsTerminating())
        {
            for (auto it = m_PersistentConnections.begin(); it != m_PersistentConnections.end(); ++it)
            {
                if (it->second == channel)
                {
                    m_PersistentConnections.erase(it);
                    break;
                }
            }
            return;
        }

        lock.unlock();

        if (channel->GetDirection() == details::Direction::Outgoing)
        {
            // reconnect
            try
            {
                ConnectWithTimer(channel->GetConnectToServerCallback(), channel->GetRemoteEndpoint(), channel);
            }
            catch (const std::exception&)
            {
                LOG_ERROR("Failed to perform socket reconnection, error: %s", boost::current_exception_diagnostic_information(true));
            }
        }
    }

    void Accept()
    {
        if (!m_Acceptor.is_open())
            return;

        const auto socket = CreateSocket();
        m_Acceptor.async_accept(*socket,
                                m_AcceptStrand.wrap(boost::bind(&Transport::ClientAccepted,
                                                    Shared::shared_from_this(),
                                                    socket,
                                                    boost::asio::placeholders::error)));
    }

    void Connect(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        auto parsedEP = ParseEP(endpoint);
        if (IsIP(parsedEP.first))
            ConnectWithTimer(callback, boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(parsedEP.first), parsedEP.second));
        else
            ResolveAndConnectWithTimer(callback, parsedEP.first, parsedEP.second);
    }

    void ConnectWithTimer(const ITransport::Callback& cb,
                          const boost::asio::ip::tcp::endpoint& ep,
                          const typename TCPChannel::Ptr& previousInstance = typename TCPChannel::Ptr())
    {
        ConnectionInitializationData initData;
        PrepareConnectionInitializationData(initData);
        initData.m_ResolvedAsioEndpoint = boost::make_shared<boost::optional<boost::asio::ip::tcp::endpoint>>(ep);
        initData.m_Callback = cb;
        initData.m_PreviousInstance = previousInstance;

        AsyncConnect(initData);
        StartConnectTimer(initData);
    }

    void ResolveAndConnectWithTimer(const ITransport::Callback& cb,
                                    const std::string& endpointServer,
                                    unsigned short endpointPort,
                                    const typename TCPChannel::Ptr& previousInstance = typename TCPChannel::Ptr())
    {
        ConnectionInitializationData initData;
        PrepareConnectionInitializationData(initData);
        initData.m_EndpointServer = endpointServer;
        initData.m_EndpointPort = endpointPort;
        initData.m_ResolvedAsioEndpoint = boost::make_shared<boost::optional<boost::asio::ip::tcp::endpoint>>();
        initData.m_Callback = cb;
        initData.m_PreviousInstance = previousInstance;

        AsyncResolveAndConnect(initData);
        StartConnectTimer(initData);
    }

    Handle CreateSocket()
    {
        const auto socket = boost::make_shared<Socket>(m_Service);

        boost::unique_lock<boost::mutex> lock(m_Mutex);
        m_Sockets.emplace_back(socket);
        return socket;
    }

    void Close()
    {
        m_Acceptor.close();

        std::vector<boost::weak_ptr<Socket>> copy;
        std::map<boost::asio::ip::address, typename TCPChannel::Ptr> connections;
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            copy.swap(m_Sockets);
            connections.swap(m_PersistentConnections);
            m_IsClosed = true;
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

        for (const auto& pair : connections)
            pair.second->Close();
    }

private:

    struct ConnectionInitializationData
    {
        boost::weak_ptr<ThisType> m_Instance;

        std::string m_EndpointServer; // e.g. "localhost"
        unsigned short m_EndpointPort;         // e.g. 443

        boost::shared_ptr<boost::optional<boost::asio::ip::tcp::endpoint>> m_ResolvedAsioEndpoint;

        ITransport::Callback m_Callback;
        boost::shared_ptr<std::atomic<bool>> m_Condition;
        boost::shared_ptr<boost::asio::deadline_timer> m_Timer;
        typename TCPChannel::Ptr m_PreviousInstance;
    };

private:

    void PrepareConnectionInitializationData(ConnectionInitializationData& initData)
    {
        initData.m_Instance = Shared::shared_from_this();
        initData.m_Condition = boost::make_shared<std::atomic<bool>>(false);
        initData.m_Timer = boost::make_shared<boost::asio::deadline_timer>(m_Service);
    }

    void StartConnectTimer(const ConnectionInitializationData& initData)
    {
        if (m_TimeoutSettings.m_ConnectionTimeout.total_milliseconds())
        {
            initData.m_Timer->expires_from_now(m_TimeoutSettings.m_ConnectionTimeout);
            initData.m_Timer->async_wait([initData](const boost::system::error_code& e)
            {
                if (e)
                    return;

                LOG_INFO("Connection timer expired");

                *initData.m_Condition = true;

                try
                {
                    BOOST_THROW_EXCEPTION(net::Disconnected("Timeout has expired"));
                }
                catch (const std::exception&)
                {
                    initData.m_Callback(IConnection::Ptr(), boost::current_exception());
                }
            });
        }
    }

    void OnAsyncConnect(const ConnectionInitializationData& initData,
                        Handle socket,
                        const boost::system::error_code& e)
    {
        const auto locked = initData.m_Instance.lock();
        if (!locked)
            return;

        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            if (m_IsClosed)
                return;
        }

        LOG_INFO("Connection to %s has %s", conv::cast<std::string>(initData.m_ResolvedAsioEndpoint->get()), (e ? "failed" : "succeeded"));

        try
        {
            if (e)
            {
                if (!*initData.m_Condition)
                {
                    const auto reconnectTimer = boost::make_shared<boost::asio::deadline_timer>(m_Service);
                    reconnectTimer->expires_from_now(boost::posix_time::seconds(5));
                    reconnectTimer->async_wait([reconnectTimer, initData, this]
                          (const boost::system::error_code& e)
                    {
                        if (const auto locked = initData.m_Instance.lock() && !*initData.m_Condition && !e)
                            AsyncResolveAndConnect(initData);
                    });
                }    
                
                return;
            }

            boost::system::error_code ignore;
            initData.m_Timer->cancel(ignore);
            ConfigureInheritance(*socket);

            if (m_KeepAlive == KeepAlive::Enabled)
            {
                const unsigned long keepaliveTimeout{ 1000 * 30 };  // 30 seconds
                const unsigned long keepaliveInterval{ 1000 };
                EnableTcpKeepAlive(socket->native_handle(), keepaliveTimeout, keepaliveInterval);
            }

            if (initData.m_PreviousInstance)
            {
                // make a reconnection packet
                initData.m_PreviousInstance->Synchronize(socket);
            }
            else
            {
                const auto connection = m_Factory->Create(socket, locked, details::Direction::Outgoing);
                connection->SetConnectToServerCallback(initData.m_Callback);

                if (Traits::Settings::IsPersistent())
                {
                    boost::unique_lock<boost::mutex> lock(m_Mutex);
                    m_PersistentConnections.emplace(connection->GetRemoteEndpoint().address(), connection);
                }

                initData.m_Callback(connection, boost::exception_ptr());
            }
        }
        catch (const std::exception&)
        {
            LOG_ERROR("Failed to connect to: %s, error: %s", conv::cast<std::string>(initData.m_ResolvedAsioEndpoint->get()), boost::current_exception_diagnostic_information());
            if (!*initData.m_Condition && !m_IsClosed)
                AsyncConnect(initData);
        }
    }

    void AsyncConnect(const ConnectionInitializationData& initData)
    {
        if (m_IsClosed || *initData.m_Condition)
            return;

        LOG_INFO("Connecting to %s", conv::cast<std::string>(initData.m_ResolvedAsioEndpoint->get()));

        const auto socket = CreateSocket();
        socket->async_connect(
            initData.m_ResolvedAsioEndpoint->get(), 
            boost::bind(&ThisType::OnAsyncConnect, this, initData, socket, _1)
        );
    }

    void OnAsyncResolveForConnect(const ConnectionInitializationData& initData,
                                  const boost::shared_ptr<boost::asio::ip::tcp::resolver>&,
                                  const boost::system::error_code& e,
                                  const boost::asio::ip::tcp::resolver::iterator& iterator)
    {
        const auto locked = initData.m_Instance.lock();
        if (!locked)
            return;

        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            if (m_IsClosed)
                return;
        }

        LOG_INFO("Resolve of IP address of %s:%s %s", initData.m_EndpointServer, initData.m_EndpointPort, (e ? "failed" : "succeeded"));

        try
        {
            if (e)
            {
                if (*initData.m_Condition)
                    return;

                const auto timer = boost::make_shared<boost::asio::deadline_timer>(m_Service);
                timer->expires_from_now(boost::posix_time::seconds(5));
                timer->async_wait([initData, timer](const boost::system::error_code&)
                                  {
                                      if (const auto locked = initData.m_Instance.lock())
                                          locked->AsyncResolveAndConnect(initData);
                                  });
                return;
            }

            *initData.m_ResolvedAsioEndpoint = boost::asio::ip::tcp::endpoint(iterator->endpoint().address(), initData.m_EndpointPort);
            AsyncConnect(initData);
        }
        catch (const std::exception&)
        {
            LOG_ERROR("Failed to connect to: %s:%s, error: %s", initData.m_EndpointServer, initData.m_EndpointPort, boost::current_exception_diagnostic_information());
            if (!*initData.m_Condition && !m_IsClosed)
                AsyncResolveAndConnect(initData);
        }
    }

    void AsyncResolveAndConnect(const ConnectionInitializationData& initData)
    {
        if (initData.m_ResolvedAsioEndpoint->is_initialized())
        {
            AsyncConnect(initData);
            return;
        }

        if (m_IsClosed || *initData.m_Condition)
            return;

        LOG_INFO("Connecting to %s:%s", initData.m_EndpointServer, initData.m_EndpointPort);

        auto resolver = boost::make_shared<boost::asio::ip::tcp::resolver>(m_Service);
        boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), initData.m_EndpointServer, std::to_string(initData.m_EndpointPort));
        resolver->async_resolve(query, boost::bind(&ThisType::OnAsyncResolveForConnect, this, initData, resolver, _1, _2));
    }

    std::pair<std::string, unsigned short> ParseEP(const std::string& endpoint)
    {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, endpoint, boost::algorithm::is_any_of(":"));
        if (parts.size() != 2)
            BOOST_THROW_EXCEPTION(Exception("Wrong endpoint") << EndpointInfo(endpoint));

        return std::make_pair(parts.front(), conv::cast<unsigned short>(parts.back()));
    }

    bool IsIP(const std::string& server)
    {
        std::vector<std::string> dummyParts;
        boost::algorithm::split(dummyParts, server, boost::algorithm::is_any_of("."));

        if (dummyParts.size() != 4)
            return false;

        for (auto& part : dummyParts)
            for (auto& c : part)
                if (!isdigit(c))
                    return false;

        return true;
    }

    //! Accept client handler
    void ClientAccepted(const Handle& client, boost::system::error_code e)
    {
        ConfigureInheritance(*client);

        // prepare to accept new client
        Accept();

        if (e)
            return; // failed to accept client

        const auto ep = boost::lexical_cast<std::string>(client->remote_endpoint());
        LOG_DEBUG("Accepting connection from: %s", ep);

        if (Traits::Settings::IsPersistent())
        {
            try
            {
                if (m_KeepAlive == KeepAlive::Enabled)
                {
                    const unsigned long keepaliveTimeout{ 1000 * 30 };  // 30 seconds
                    const unsigned long keepaliveInterval{ 1000 };
                    EnableTcpKeepAlive(client->native_handle(), keepaliveTimeout, keepaliveInterval);
                }

                const auto instance = Shared::shared_from_this();
                const auto timer = boost::make_shared<boost::asio::deadline_timer>(client->get_io_service());

                timer->expires_from_now(boost::posix_time::minutes(1));
                timer->async_wait([timer, client, this, instance](boost::system::error_code e)
                {
                    if (e)
                        return;

                    const auto ep = client->remote_endpoint(e);
                    LOG_ERROR("Failed to synchronize within 1 minute, ep: %s", ep);
                    client->close(e);
                });

                // read asynchronously, but explicitly wait for result right here, because we don't want concurrency here
                auto future = boost::async([client, ep]()
                {
                    net::details::RestorationHeader header = {};
                    static const auto headerSize = sizeof(net::details::RestorationHeader);

                    LOG_INFO("Reading sync packet from: %s", ep);
                    const auto size = boost::asio::read(*client, boost::asio::buffer(&header, headerSize), boost::asio::transfer_exactly(headerSize));
                    LOG_INFO("Received sync packet from: %s, byte size: %s, message size: %s, bytes: %s, crc: %s", ep, size, header.m_Size, header.m_BytesReceived, header.m_Crc32);

                    return header;
                });

                while (!future.is_ready())
                {
                    if (m_Service.stopped())
                        return;

                    try
                    {
                        if (!m_Service.poll_one())
                            boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
                    }
                    catch (const std::exception&)
                    {
                        LOG_ERROR("Failed to wait for sync packet: %s", boost::current_exception_diagnostic_information());
                    }
                }

                boost::system::error_code ignore;
                timer->cancel(ignore);

                const auto& header = future.get();

                // validate what we have received
                std::size_t size = header.m_Size;
                if (!size)
                {
                    if (header.Crc32() != header.m_Crc32)
                        BOOST_THROW_EXCEPTION(net::Exception(TXT("Wrong sync header, expected crc: %s, got: %s", header.m_Crc32, header.Crc32())));

                    boost::unique_lock<boost::mutex> lock(m_Mutex);
                    const auto it = m_PersistentConnections.find(client->remote_endpoint().address());
                    if (it == m_PersistentConnections.end())
                    {
                        LOG_WARNING("Detected client reconnection from: %s, but there is no such client, assuming it's a new client and closing", ep);

                        const auto result = m_Factory->Create(client, instance, details::Direction::Incoming);
                        m_PersistentConnections.emplace(result->GetRemoteEndpoint().address(), result);
                        lock.unlock();

                        result->SendSyncPacket(client);
                        m_ClientConnectedCallback(result, boost::exception_ptr());
                    }
                    else
                    {
                        it->second->SetHandle(client);
                        it->second->SendSyncPacket(client);
                        it->second->HandleRestorationHeader(header, e);
                    }
                }
                else
                {
                    // this is a new client, because first header is not an synchronization packet
                    boost::unique_lock<boost::mutex> lock(m_Mutex);
                    const auto it = m_PersistentConnections.find(client->remote_endpoint().address());
                    if (it == m_PersistentConnections.end())
                    {
                        LOG_INFO("Detected new client connection from: %s, creating new persistent one", ep);

                        const auto result = m_Factory->Create(client, instance, details::Direction::Incoming);
                        m_PersistentConnections.emplace(result->GetRemoteEndpoint().address(), result);
                        lock.unlock();

                        // put header bytes back to the channel
                        result->PutBackHeader(header);

                        m_ClientConnectedCallback(result, boost::exception_ptr());
                    }
                    else
                    {
                        LOG_INFO("Detected new client connection from: %s, dropping old persistent one", ep);

                        it->second->Terminate();
                        m_PersistentConnections.erase(it);

                        const auto result = m_Factory->Create(client, instance, details::Direction::Incoming);
                        m_PersistentConnections.emplace(result->GetRemoteEndpoint().address(), result);
                        lock.unlock();

                        // put header bytes back to the channel
                        result->PutBackHeader(header);

                        m_ClientConnectedCallback(result, boost::exception_ptr());
                    }
                }
            }
            catch (const std::exception&)
            {
                LOG_ERROR("Failed to handle client connection: %s, error: %s", boost::current_exception_diagnostic_information(), e.message());
                client->close(e);
            }
        }
        else
        {
            const auto result = m_Factory->Create(client, Shared::shared_from_this(), details::Direction::Incoming);
            m_ClientConnectedCallback(result, boost::exception_ptr());
        }
    }

#ifdef _WIN32

    void EnableTcpKeepAlive(SOCKET socket, unsigned long timeout, unsigned long interval)
    {
        tcp_keepalive keepalive;
        keepalive.onoff = 1;
        keepalive.keepalivetime = timeout;
        keepalive.keepaliveinterval = interval;
        DWORD resultSize;
        const int result{ WSAIoctl(
            socket,
            SIO_KEEPALIVE_VALS,
            &keepalive, sizeof keepalive,
            nullptr, 0,  // unused
            &resultSize,<
            nullptr, nullptr) };  // unused

        if (result != 0)
            BOOST_THROW_EXCEPTION(net::Exception("Failed to enable TCP keepalive.")
            << net::SysErrorInfo(boost::system::error_code(WSAGetLastError(), boost::system::system_category())));
    }

#else

    void EnableTcpKeepAlive(int socket, int timeout, int interval)
    {
        const int enableKeepalive{ 1 };
        int result{ setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &enableKeepalive, sizeof enableKeepalive) };
        if (result != 0)
            BOOST_THROW_EXCEPTION(net::Exception("Failed to enable TCP keepalive.")
            << net::SysErrorInfo(boost::system::error_code(errno, boost::system::system_category())));

        result = setsockopt(socket, SOL_TCP, TCP_KEEPIDLE, &timeout, sizeof timeout);
        if (result != 0)
            BOOST_THROW_EXCEPTION(net::Exception("Failed to enable TCP timeout.")
            << net::SysErrorInfo(boost::system::error_code(errno, boost::system::system_category())));

        result = setsockopt(socket, SOL_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
        if (result != 0)
            BOOST_THROW_EXCEPTION(net::Exception("Failed to enable TCP interval.")
            << net::SysErrorInfo(boost::system::error_code(errno, boost::system::system_category())));
    }

#endif  // _WIN32

private:
    boost::asio::io_service& m_Service;
    boost::asio::ip::tcp::acceptor m_Acceptor;
    boost::asio::io_service::strand m_AcceptStrand;

    ITransport::Callback m_ClientConnectedCallback;
    std::vector<boost::weak_ptr<Socket>> m_Sockets;
    boost::mutex m_Mutex;
    typename details::IFactory<TCPChannel, 
                               Handle, 
                               Ptr, 
                               details::Direction>::Ptr m_Factory;
    KeepAlive m_KeepAlive;
    HandleInheritance m_HandleInheritance;
    const TimeoutSettings m_TimeoutSettings;
    bool m_IsClosed;

    std::map<boost::asio::ip::address, typename TCPChannel::Ptr> m_PersistentConnections;
};

} // namespace tcp
} // namespace net
