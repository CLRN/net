#pragma once

#include "net/details/channel_base.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

SET_LOGGING_MODULE("net");

//! Channel type
template<typename Owner>
class SyncStream : public net::details::Channel<typename Owner::Handle,
                                                typename Owner::Queue,
                                                typename Owner::Settings>
{
public:
    typedef details::Channel<typename Owner::Handle,
                             typename Owner::Queue,
                             typename Owner::Settings> Base;
    typedef boost::weak_ptr<Owner> OwnerPtr;

    template<typename ... Args>
    SyncStream(Args... args)
        : Base(args...)
        , m_Owner(hlp::Param<OwnerPtr>::Unpack(args...))
    {
    }

    virtual void Close() override
    {
        boost::system::error_code e;
        Base::m_IoObject->shutdown(boost::asio::ip::tcp::socket::shutdown_both, e);
        Base::Close();
    }

    //! Begin asynchronous read to buffer
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->async_read_some(
            buffer,
            boost::bind(&Base::ReadMessageCallback, Base::shared_from_this(), _1, _2)
        );   
    }


    virtual void Write(const MemHolder& holder) override
    {
        boost::system::error_code e;
        const auto written = boost::asio::write(*Base::m_IoObject,
                                                boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                boost::asio::transfer_all(),
                                                e);
        if (written != holder.m_Size || e)
        {
            if (const auto o = m_Owner.lock())
                o->ConnectionClosed(Base::shared_from_this(), e);
        }
    }

    virtual void ConnectionClosed(const boost::system::error_code& e, std::size_t bytes) override
    {
        if (const auto owner = m_Owner.lock())
        {
            LOG_INFO("Connection closed, bytes: %s, error: (%s) %s", bytes, e.value(), e.message());
            owner->ConnectionClosed(Base::shared_from_this(), e);
        }
        Base::m_Callback(IConnection::StreamPtr());
    }

    typename Owner::Handle GetSocket() const
    {
        return Base::m_IoObject;
    }

private:
    const OwnerPtr m_Owner;
};

} // namespace channels
} // namespace net
