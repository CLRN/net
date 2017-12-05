#include "net/ipc.hpp"
#include "net/tcp.hpp"
#include "net/details/stream.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <boost/thread.hpp>

std::size_t CopyStream(std::istream& is, net::IConnection& c)
{
    const auto size = net::StreamSize(is);
    std::vector<char> buffer(size);

    std::copy(std::istream_iterator<char>(is), std::istream_iterator<char>(), buffer.begin());
    c.Prepare(buffer.size())->Write(buffer.data(), buffer.size());
    return size;
}

TEST(Transport, Echo)
{
    boost::asio::io_service svc;
    const auto transport = net::tcp::CreateTransport(std::ref(svc));

    std::string ep;
    do
    {
        try
        {
            static short port = 10000;
            ep = "127.0.0.1:" + conv::cast<std::string>(port++);
            transport->Receive(ep, [](const net::IConnection::Ptr& connection, const boost::exception_ptr& e)
            {
                std::cout << "connected: " << connection->GetInfo() << std::endl;
                connection->Receive([connection](const net::IConnection::StreamPtr& stream)
                                    {
                                        if (stream)
                                        {
                                            std::cout << "echo: " << net::StreamSize(*stream) << std::endl;
                                            CopyStream(*stream, *connection);
                                        }
                                    });
            });
        }
        catch (const boost::system::system_error&)
        {
            continue;
        }
    }
    while (false);

    boost::promise<std::string> promise;
    std::string data = "Hello world!\n";
    for (int i = 0; i < 20; ++i)
        data += data;

    std::cout << "data size: " << data.size() << std::endl;

    transport->Connect(ep, [&promise, &data](const net::IConnection::Ptr& connection, const boost::exception_ptr& e)
    {
        if (e)
            return;

        connection->Receive([&promise](const net::IConnection::StreamPtr& stream)
                            {
                                if (stream)
                                {
                                    std::string tmp;
                                    tmp.assign(std::istream_iterator<char>(*stream), std::istream_iterator<char>());
                                    promise.set_value(tmp);
                                }

                            });

        connection->Prepare(data.size())->Write(data.c_str(), data.size());
    });

    auto future = promise.get_future();
    while (!future.is_ready())
        svc.poll();

    EXPECT_EQ(future.get(), data);
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
