#include "net/details/transport.hpp"

#include "net/transports/tcp.hpp"
#include "net/transports/udp.hpp"

#include "net/channels/sync_stream.hpp"
#include "net/channels/sync_message.hpp"
#include "net/channels/sync_random.hpp"
#include "net/channels/async_stream.hpp"

#include <mutex>
#include <chrono>

#include <boost/make_unique.hpp>

std::size_t StreamSize(std::istream& is)
{
    const auto pos = is.tellg();
    is.seekg(0, std::ios::end);
    const auto size = is.tellg() - pos;
    is.seekg(pos);
    return size;
}

std::size_t CopyStream(std::istream& is, net::IConnection& c)
{
    const auto size = StreamSize(is);

    const auto mem = c.Prepare(size);
    std::copy(std::istream_iterator<char>(is), std::istream_iterator<char>(), mem.get());
    c.Send(mem, size);
    return size;
}

template<typename Transport>
class Server
{
public:
    Server(boost::asio::io_service& s, bool echo)
        : m_Service(s)
        , m_Transport(s)
        , m_BytesReceived()
        , m_PacketsReceived()
        , m_SendEcho(echo)
    {}

    void Start(const std::string& ep)
    {
        m_Transport.Receive(ep, boost::bind(&Server::OnClientConnected, this, _1, _2));
    }

    void OnClientConnected(const net::IConnection::Ptr& c, const boost::exception_ptr& e)
    {
        if (e)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
            return;
        }

        c->Receive(boost::bind(&Server::OnDataRead, this, _1, c));
    }

    void OnDataRead(const typename Transport::Stream& stream, const net::IConnection::Ptr& c)
    {
        if (!stream)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
            return;
        }

        if (m_SendEcho)
            m_BytesReceived += CopyStream(*stream, *c);
        else
            m_BytesReceived += StreamSize(*stream);

        ++m_PacketsReceived;
        //std::cout << "copying: " << m_BytesReceived << std::endl;
    }

    void Stat(unsigned ms)
    {
        std::cout
        << "Server bytes received : " << m_BytesReceived << ", packets: " << m_PacketsReceived << std::endl
        << "Server speed: " << m_BytesReceived / ms << " KBytes/sec, "
        << m_PacketsReceived * 1000 / ms << " Packets/sec" <<  std::endl;
    }

    void Stop()
    {
        m_Transport.Close();
    }
private:
    boost::asio::io_service& m_Service;
    Transport m_Transport;
    std::atomic<unsigned> m_BytesReceived;
    std::atomic<unsigned> m_PacketsReceived;
    bool m_SendEcho;
};

template<typename Transport>
class Client
{
public:
    Client(boost::asio::io_service& s, unsigned size)
        : m_Service(s)
        , m_Transport(s)
        , m_BytesSent()
        , m_BytesReceived()
        , m_PacketSize(size)
    {}

    void Start(const std::string& ep)
    {
        m_Connection = m_Transport.Connect(ep);

        boost::thread(boost::bind(&Client::Send, this));
        m_Connection->Receive(boost::bind(&Client::OnDataRead, this, _1));
    }

    void Send()
    {
        const auto data = m_Connection->Prepare(m_PacketSize);
        try
        {
            while (!m_Service.stopped())
            {
                m_Connection->Send(data, m_PacketSize);
                m_BytesSent += m_PacketSize;
            }
        }
        catch (const net::Exception&)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
        }
    }

    void OnDataRead(const typename Transport::Stream& stream)
    {
        if (!stream)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
            return;
        }

        m_BytesReceived += StreamSize(*stream);
        ++m_PacketsReceived;
        //std::cout << "receiving: " << m_BytesReceived << std::endl;
    }

    void Stat(unsigned ms)
    {
        std::cout
        << "Client sent: " << m_BytesSent << std::endl
        << "Client received: " << m_BytesReceived << std::endl
        << "Send speed: " << m_BytesSent / ms << " KBytes/sec"  << std::endl
        << "Client speed: " << m_BytesReceived / ms << " KBytes/sec, "
        << m_PacketsReceived * 1000 / ms << " Packets/sec" << std::endl
        << "Client bytes received : " << m_BytesReceived << ", packets: " << m_PacketsReceived << std::endl;
    }

    void Stop()
    {
        if (m_Connection)
            m_Connection->Close();
    }

private:
    boost::asio::io_service& m_Service;
    Transport m_Transport;
    net::IConnection::Ptr m_Connection;
    std::atomic<unsigned> m_BytesSent;
    std::atomic<unsigned> m_BytesReceived;
    std::atomic<unsigned> m_PacketsReceived;
    unsigned m_PacketSize;
};

template <typename T>
class EchoSpeedTest : public ::testing::Test
{
public:
    EchoSpeedTest() : m_IsUsingEcho(true)
    {
    }

    void Do(unsigned seconds, unsigned size)
    {
        const auto ep = "127.0.0.1:10000";

        m_Server.reset(new Server<T>(m_Service, m_IsUsingEcho));
        m_Client.reset(new Client<T>(m_Service, size));

        m_Work = boost::make_unique<boost::asio::io_service::work>(m_Service);
        for (unsigned i = 0; i < boost::thread::hardware_concurrency() * 2; ++i)
            m_Pool.create_thread(boost::bind(&boost::asio::io_service::run, &m_Service));

        m_Server->Start(ep);
        m_Service.post(boost::bind(&Client<T>::Start, m_Client.get(), ep));

        const auto start = std::chrono::system_clock::now();
        boost::this_thread::sleep(boost::posix_time::seconds(seconds));
        const auto now = std::chrono::system_clock::now();

        m_Service.stop();
        m_Work.reset();

        m_Server->Stop();
        m_Client->Stop();
        m_Pool.join_all();

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
        std::cout << "Packet size: " << size << ", elapsed: " << ms.count() << std::endl;

        m_Server->Stat(ms.count());
        m_Client->Stat(ms.count());
    }

protected:
    bool m_IsUsingEcho;

private:
    boost::asio::io_service m_Service;
    boost::thread_group m_Pool;
    std::unique_ptr<boost::asio::io_service::work> m_Work;

    std::unique_ptr<Server<T>> m_Server;
    std::unique_ptr<Client<T>> m_Client;
};

template <typename T>
class SpeedTest : public EchoSpeedTest<T>
{
public:
    SpeedTest()
    {
        EchoSpeedTest<T>::m_IsUsingEcho = false;
    }

};

TYPED_TEST_CASE_P(EchoSpeedTest);
TYPED_TEST_CASE_P(SpeedTest);

TYPED_TEST_P(EchoSpeedTest, VerySmallPacket)
{
    this->Do(10, 10);
}

TYPED_TEST_P(EchoSpeedTest, SmallPacket)
{
    this->Do(10, 100);
}

TYPED_TEST_P(EchoSpeedTest, OptimalPacket)
{
    this->Do(10, 4092);
}

TYPED_TEST_P(EchoSpeedTest, MediumPacket)
{
    this->Do(10, 4096);
}

TYPED_TEST_P(EchoSpeedTest, BigPacket)
{
    this->Do(10, 1024 * 1024);
}

TYPED_TEST_P(SpeedTest, VerySmallPacket)
{
    this->Do(10, 10);
}

TYPED_TEST_P(SpeedTest, SmallPacket)
{
    this->Do(10, 100);
}

TYPED_TEST_P(SpeedTest, OptimalPacket)
{
    this->Do(10, 4092);
}

TYPED_TEST_P(SpeedTest, MediumPacket)
{
    this->Do(10, 4096);
}

TYPED_TEST_P(SpeedTest, BigPacket)
{
    this->Do(10, 1024 * 1024);
}


REGISTER_TYPED_TEST_CASE_P(EchoSpeedTest,
                           VerySmallPacket, SmallPacket, OptimalPacket, MediumPacket, BigPacket);

REGISTER_TYPED_TEST_CASE_P(SpeedTest,
                           VerySmallPacket, SmallPacket, OptimalPacket, MediumPacket, BigPacket);

typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> TcpTransport;
typedef ::testing::Types<TcpTransport> Transports;

INSTANTIATE_TYPED_TEST_CASE_P(TransportSpeedTest, EchoSpeedTest, Transports);
INSTANTIATE_TYPED_TEST_CASE_P(TransportSpeedTest, SpeedTest, Transports);

/*


void Do1()
{
    boost::asio::io_service svc;
    typedef net::Transport<net::udp::Transport, net::channels::SyncMessage> Transport;

    Transport transport(std::ref(svc));
    const auto connection = transport.Connect("127.0.0.1:10000");
    connection->Receive([](const Transport::Stream& stream){

    });

    const auto data = connection->Prepare(1);
    connection->Send(data, 1);
}

void Do2()
{
    boost::asio::io_service svc;
    typedef net::Transport<net::tcp::Transport, net::channels::AsyncStream> Transport;

    Transport transport(std::ref(svc));
    const auto connection = transport.Connect("127.0.0.1:10000");
    connection->Receive([](const Transport::Stream& stream){

    });

    const auto data = connection->Prepare(1);
    connection->Send(data, 1);
}*/
