#include "net/details/transport_impl.hpp"

#include "net/transports/tcp.hpp"
#include "net/transports/udp.hpp"

#include "net/ipc.hpp"
#include "net/tcp.hpp"

#include "net/channels/sync_stream.hpp"
#include "net/channels/sync_message.hpp"
#include "net/channels/sync_random.hpp"
#include "net/channels/async_stream.hpp"

#include <mutex>
#include <chrono>
#include <atomic>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <boost/make_unique.hpp>
#include <boost/enable_shared_from_this.hpp>

std::size_t StreamSize(std::istream& is)
{
    const auto pos = is.tellg();
    is.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(is.tellg() - pos);
    is.seekg(pos);
    return size;
}

std::size_t CopyStream(std::istream& is, net::IConnection& c)
{
    const auto size = StreamSize(is);
    std::vector<char> buffer(size);

    std::copy(std::istream_iterator<char>(is), std::istream_iterator<char>(), buffer.begin());
    c.Prepare(buffer.size())->Write(buffer.data(), buffer.size());
    return size;
}

struct TestResult
{
    typedef std::vector<TestResult> List;


    void Print() const
    {
        std::cout << "Test name: " << m_Name << std::endl;


        std::cout
        << "\t\tSend speed: " << m_ByteSendSpeed << " KBytes/sec, " << m_PacketSendSpeed << " Packets/sec" << std::endl;

        std::cout
        << "\t\tReceive speed: " << m_BytesSpeed << " KBytes/sec, " << m_PacketsSpeed << " Packets/sec" << std::endl;
    }

    std::string m_Name;
    uint64_t m_ByteSendSpeed;
    uint64_t m_PacketSendSpeed;
    uint64_t m_BytesSpeed;
    uint64_t m_PacketsSpeed;
};

struct ResultHolder
{
    ~ResultHolder()
    {
        for (const auto& r : m_Results)
            r.Print();
    }
    static ResultHolder& Instance()
    {
        static ResultHolder instance;
        return instance;
    }

    void Add(TestResult&& r)
    {
        m_Results.emplace_back(std::move(r));
    }

private:
    TestResult::List m_Results;
};

template<typename Transport>
class Server : public boost::enable_shared_from_this<Server<Transport>>
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
        m_Transport.Receive(ep, boost::bind(&Server::OnClientConnected, this->shared_from_this(), _1, _2));
    }

    void OnClientConnected(const net::IConnection::Ptr& c, const boost::exception_ptr& e)
    {
        if (e)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
            return;
        }

        c->Receive(boost::bind(&Server::OnDataRead, this->shared_from_this(), _1, c));
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

    uint64_t GetBytesSpeed(uint64_t ms) const
    {
        return m_BytesReceived / ms;
    }

    uint64_t GetPacketSpeed(uint64_t ms) const
    {
        return m_PacketsReceived * 1000 / ms;
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
class Client : public boost::enable_shared_from_this<Client<Transport>>
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

        m_SendThread = boost::thread(boost::bind(&Client::Send, this->shared_from_this()));
        m_Connection->Receive(boost::bind(&Client::OnDataRead, this->shared_from_this(), _1));
    }

/*
    void Send()
    {
        const auto data = m_Connection->Prepare(m_PacketSize);
        try
        {
            while (!m_Service.stopped())
            {
                m_Connection->Send(data);
                m_BytesSent += m_PacketSize;
            }
        }
        catch (const net::Exception&)
        {
            std::cout << boost::current_exception_diagnostic_information(true) << std::endl;
        }
    }
*/

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

    uint64_t GetSendBytesSpeed(uint64_t ms) const
    {
        return m_BytesSent / ms;
    }

    uint64_t GetSendPacketSpeed(uint64_t ms) const
    {
        return (m_BytesSent / m_PacketSize) * 1000 / ms;
    }

    uint64_t GetRecvBytesSpeed(uint64_t ms) const
    {
        return m_BytesReceived / ms;
    }

    uint64_t GetRecvPacketSpeed(uint64_t ms) const
    {
        return m_PacketsReceived * 1000 / ms;
    }

    void Stop()
    {
        if (m_Connection)
            m_Connection->Close();
        m_SendThread.join();
    }

private:
    boost::asio::io_service& m_Service;
    Transport m_Transport;
    net::IConnection::Ptr m_Connection;
    std::atomic<unsigned> m_BytesSent;
    std::atomic<unsigned> m_BytesReceived;
    std::atomic<unsigned> m_PacketsReceived;
    unsigned m_PacketSize;
    boost::thread m_SendThread;
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
        boost::asio::io_service service;
        boost::thread_group pool;
        std::unique_ptr<boost::asio::io_service::work> work;

        std::string ep;
        const auto server = boost::make_shared<Server<T>>(service, m_IsUsingEcho);
        do
        {
            try
            {
                static short port = 10000;
                ep = "127.0.0.1:" + conv::cast<std::string>(port++);
                server->Start(ep);
            }
            catch (const boost::system::system_error&)
            {
                continue;
            }
        }
        while (false);

        const auto client = boost::make_shared<Client<T>>(service, size);

        work = boost::make_unique<boost::asio::io_service::work>(service);
        for (unsigned i = 0; i < boost::thread::hardware_concurrency() * 2; ++i)
            pool.create_thread(boost::bind(&boost::asio::io_service::run, &service));

        service.post(boost::bind(&Client<T>::Start, client, ep));

        const auto start = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::time_point now;
        while (now - start < std::chrono::seconds(seconds))
        {
            boost::this_thread::sleep_for(boost::chrono::milliseconds(10));
            now = std::chrono::high_resolution_clock::now();
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);

        std::cout << ms.count() << std::endl;

        service.stop();
        work.reset();

        server->Stop();
        client->Stop();
        pool.join_all();

        const auto* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        TestResult r =
        {
            std::string(test_info->test_case_name()) + ":" + test_info->name(),
            client->GetSendBytesSpeed(ms.count()),
            client->GetSendPacketSpeed(ms.count()),
            m_IsUsingEcho ? client->GetRecvBytesSpeed(ms.count()) : server->GetBytesSpeed(ms.count()),
            m_IsUsingEcho ? client->GetRecvPacketSpeed(ms.count()) : server->GetPacketSpeed(ms.count()),
        };

        ResultHolder::Instance().Add(std::move(r));
    }

protected:
    bool m_IsUsingEcho;
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

static const int seconds = 10;
/*
TYPED_TEST_P(EchoSpeedTest, VerySmallPacket)
{
    this->Do(seconds, 10);
}

TYPED_TEST_P(EchoSpeedTest, SmallPacket)
{
    this->Do(seconds, 100);
}

TYPED_TEST_P(EchoSpeedTest, OptimalPacket)
{
    this->Do(seconds, 4092);
}

TYPED_TEST_P(EchoSpeedTest, MediumPacket)
{
    this->Do(seconds, 4096);
}

TYPED_TEST_P(EchoSpeedTest, BigPacket)
{
    this->Do(seconds, 1024 * 1024);
}
*/
TYPED_TEST_P(SpeedTest, VerySmallPacket)
{
    this->Do(seconds, 10);
}
/*
TYPED_TEST_P(SpeedTest, SmallPacket)
{
    this->Do(seconds, 100);
}

TYPED_TEST_P(SpeedTest, OptimalPacket)
{
    this->Do(seconds, 4092);
}

TYPED_TEST_P(SpeedTest, MediumPacket)
{
    this->Do(seconds, 4096);
}

TYPED_TEST_P(SpeedTest, BigPacket)
{
    this->Do(seconds, 1024 * 1024);
}
*/
//
//REGISTER_TYPED_TEST_CASE_P(EchoSpeedTest,
//                           VerySmallPacket, SmallPacket, OptimalPacket, MediumPacket, BigPacket);

REGISTER_TYPED_TEST_CASE_P(SpeedTest,
                           VerySmallPacket/*, SmallPacket, OptimalPacket, MediumPacket, BigPacket*/);

typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> TcpSyncTransport;
typedef net::Transport<net::tcp::Transport, net::channels::AsyncStream> TcpAsyncTransport;
typedef ::testing::Types<TcpAsyncTransport> Transports;

//INSTANTIATE_TYPED_TEST_CASE_P(TransportSpeedTest, EchoSpeedTest, Transports);
//INSTANTIATE_TYPED_TEST_CASE_P(TransportSpeedTest, SpeedTest, Transports);




// void Do1()
// {
//     boost::asio::io_service svc;
//     typedef net::Transport<net::udp::Transport, net::channels::SyncMessage> Transport;
//
//     Transport transport(svc);
//     const auto connection = transport.Connect("127.0.0.1:10000");
//     connection->Receive([](const Transport::Stream& stream){
//
//     });
//
//     const auto data = connection->Prepare(1);
//     connection->Send(data);
// }


void Do2()
{
    boost::asio::io_service svc;
    const auto transport = net::tcp::CreateTransport(std::ref(svc));

    transport->Receive("127.0.0.1:20000", [&](const net::IConnection::Ptr& connection, const boost::exception_ptr& e){
        std::cout << "connected: " << connection->GetInfo() << std::endl;
        connection->Receive([&](const net::IConnection::StreamPtr& stream)
        {
            if (stream)
                std::cout << "received: " << StreamSize(*stream) << std::endl;
            svc.stop();
        });
    });

    transport->Connect("127.0.0.1:20000", [](const net::IConnection::Ptr& connection, const boost::exception_ptr& e)
    {
        if (e)
            return;

        connection->Receive([](const net::IConnection::StreamPtr& stream)
        {

        });

        connection->Prepare(1)->Write("1", 1);
    });

    svc.run();
}

TEST(Transport, Compile)
{
    Do2();
}

#ifdef WIN32
TEST(Transport, SimpleIPC)
{
    boost::asio::io_service svc;

    const auto server = net::ipc::CreateTransport(std::ref(svc));
    server->Receive("test1", [](const net::IConnection::Ptr& client, const boost::exception_ptr& e)
    {
        if (!e)
        {
            client->Receive([client](const net::IConnection::StreamPtr& stream)
            {
                if (!stream)
                    return;

                std::vector<char> data;
                std::copy(std::istream_iterator<char>(*stream), std::istream_iterator<char>(), std::back_inserter(data));

                client->Prepare(data.size())->Write(data.data(), data.size());
            });
        }
    });

    const auto client = net::ipc::CreateTransport(std::ref(svc));
    client->Connect("test1", [](const net::IConnection::Ptr& connection, const boost::exception_ptr& e)
    {
        if (e)
            return;

        connection->Receive([connection](const net::IConnection::StreamPtr& stream) mutable
        {
            if (!stream)
                return;

            std::string data;
            std::copy(std::istream_iterator<char>(*stream), std::istream_iterator<char>(), std::back_inserter(data));
            EXPECT_EQ(data, "hello!");
            connection->Close();
        });

        const std::string text = "hello!";

        connection->Prepare(text.size())->Write(text.c_str(), text.size());
    });


    svc.run();
}


TEST(Transport, HugeDataIPC)
{
    boost::asio::io_service svc;
    unsigned count = 1000;
    std::atomic<unsigned> counter(0);
    const std::vector<char> text(1024 * 10, 'x');

    const auto server = net::ipc::CreateTransport(std::ref(svc));
    server->Receive("test2", [](const net::IConnection::Ptr& client, const boost::exception_ptr& e)
    {
        if (!e)
        {
            client->Receive([client](const net::IConnection::StreamPtr& stream)
            {
                if (!stream)
                    return;

                std::size_t sent = 0;
                static const std::size_t bufferSize = 1024;

                const auto pos = stream->tellg();
                stream->seekg(0, std::ios::end);
                const auto size = static_cast<std::size_t>(stream->tellg() - pos);
                stream->seekg(pos);

                if (!size)
                    assert(false);

                std::vector<char> buffer(size);
                stream->read(buffer.data(), size);
                client->Prepare(size)->Write(buffer.data(), size);
            });
        }
    });

    const auto client = net::ipc::CreateTransport(std::ref(svc));
    client->Connect("test2", [&](const net::IConnection::Ptr& connection, const boost::exception_ptr& e)
    {
        if (e)
            return;

        connection->Receive([connection, &count, &counter, &text](const net::IConnection::StreamPtr& stream)
        {
            if (!stream)
                return;

            const auto pos = stream->tellg();
            stream->seekg(0, std::ios::end);
            const auto size = static_cast<std::size_t>(stream->tellg() - pos);
            stream->seekg(pos);

            std::vector<char> data(size);
            stream->read(data.data(), data.size());
            EXPECT_EQ(data, text);

            if (++counter == count)
                connection->Close();
        });

        for (int i = 0; i < count; ++i)
            connection->Prepare(text.size())->Write(text.data(), text.size());
    });

    svc.run();
}
#endif // WIN32
