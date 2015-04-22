#include "net/details/transport.hpp"

#include "net/transports/tcp.hpp"
#include "net/transports/udp.hpp"

#include "net/channels/sync_stream.hpp"
#include "net/channels/sync_message.hpp"
#include "net/channels/sync_random.hpp"
#include "net/channels/async_stream.hpp"

void CopyStream(std::istream& is, net::IConnection& c)
{
    const auto pos = is.tellg();
    is.seekg(0, std::ios::end);
    const auto size = is.tellg() - pos;
    is.seekg(pos);

    const auto mem = c.Prepare(size);
    std::copy(std::istream_iterator<char>(is), std::istream_iterator<char>(), mem.get());
    c.Send(mem, size);
}

TEST(TCPTransportSync, Echo)
{
    boost::asio::io_service svc;
    typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> Transport;

    Transport transport(svc);
    transport.Receive("127.0.0.1:10000", [](const net::IConnection::Ptr& c, const boost::exception_ptr& e){
        c->Receive([c](const Transport::Stream& stream){
            if (!stream)
                return;

            CopyStream(*stream, *c);
        });
    });

    const auto connection = transport.Connect("127.0.0.1:10000");

    const auto data = connection->Prepare(1);
    connection->Send(data, 1);

    svc.run();
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
