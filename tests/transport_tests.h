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

class Server
{
public:
    typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> Transport;

    Server(boost::asio::io_service& s)
        : m_Service(s)
        , m_Transport(s)
        , m_BytesReceived()
    {}

    void Start()
    {
        m_Transport.Receive("127.0.0.1:10000", boost::bind(&Server::OnClientConnected, this, _1, _2));
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

    void OnDataRead(const Transport::Stream& stream, const net::IConnection::Ptr& c)
    {
        if (!stream)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
            return;
        }

        m_BytesReceived += CopyStream(*stream, *c);
        //std::cout << "copying: " << m_BytesReceived << std::endl;
    }

    void Stat(unsigned ms)
    {
        std::cout
        << "Server received : " << m_BytesReceived << std::endl
        << "Server speed: " << m_BytesReceived / ms << " KBytes/sec" <<  std::endl;
    }

    void Stop()
    {
        m_Transport.Close();
    }
private:
    boost::asio::io_service& m_Service;
    Transport m_Transport;
    std::atomic<unsigned> m_BytesReceived;
};

class Client
{
public:
    typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> Transport;

    Client(boost::asio::io_service& s)
        : m_Service(s)
        , m_Transport(s)
        , m_BytesSent()
        , m_BytesReceived()
    {}

    void Connect()
    {
        m_Connection = m_Transport.Connect("127.0.0.1:10000");
        Send();
        m_Connection->Receive(boost::bind(&Client::OnDataRead, this, _1));
    }

    void Send()
    {
        for (int i = 0; i < 10; ++i)
        {
            const auto packetSize = 4096;
            const auto data = m_Connection->Prepare(packetSize);
            m_Connection->Send(data, packetSize);
            m_BytesSent += packetSize;

            //std::cout << "sending: " << m_BytesSent << std::endl;
        }
        m_Service.post(boost::bind(&Client::Send, this));
    }

    void OnDataRead(const Transport::Stream& stream)
    {
        if (!stream)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
            return;
        }

        m_BytesReceived += StreamSize(*stream);
        //std::cout << "receiving: " << m_BytesReceived << std::endl;
    }

    void Stat(unsigned ms)
    {
        std::cout
        << "Client sent: " << m_BytesSent << std::endl
        << "Client received: " << m_BytesReceived << std::endl
        << "Send speed: " << m_BytesSent / ms << " KBytes/sec"  << std::endl
        << "Client speed: " << m_BytesReceived / ms << " KBytes/sec"  << std::endl;

    }

    void Stop()
    {
        m_Connection->Close();
    }

private:
    boost::asio::io_service& m_Service;
    Transport m_Transport;
    net::IConnection::Ptr m_Connection;
    std::atomic<unsigned> m_BytesSent;
    std::atomic<unsigned> m_BytesReceived;
};

TEST(TCPTransportSync, Echo)
{
    boost::asio::io_service svc;
    boost::thread_group pool;

    auto work = boost::make_unique<boost::asio::io_service::work>(svc);
    for (unsigned i = 0; i < boost::thread::hardware_concurrency() * 2; ++i)
        pool.create_thread(boost::bind(&boost::asio::io_service::run, &svc));

    Server s(svc);
    Client c(svc);

    svc.post(boost::bind(&Server::Start, &s));
    svc.post(boost::bind(&Client::Connect, &c));

    const auto start = std::chrono::system_clock::now();
    boost::this_thread::sleep(boost::posix_time::seconds(10));
    const auto now = std::chrono::system_clock::now();

    svc.stop();
    work.reset();

    s.Stop();
    c.Stop();
    pool.join_all();

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    std::cout << "Elapsed: " << ms.count() << std::endl;

    c.Stat(ms.count());
    s.Stat(ms.count());
}


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
