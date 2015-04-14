#include "net/transport.hpp"

#include "net/transports/tcp.hpp"
#include "net/transports/udp.hpp"

#include "net/channels/sync_stream.hpp"
#include "net/channels/sync_message.hpp"
#include "net/channels/sync_random.hpp"

void Do()
{
    boost::asio::io_service svc;
    typedef net::Transport<net::tcp::Transport, net::channels::SyncStream> Transport;

    Transport transport(std::ref(svc));
    const auto connection = transport.Connect("127.0.0.1:10000");
    connection->Receive([](const Transport::Stream& stream){
    
    });

    const auto data = connection->Prepare(1);
    connection->Send(data, 1);
}

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
