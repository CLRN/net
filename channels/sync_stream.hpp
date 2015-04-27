#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

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
    SyncStream(const Args&... args)
        : Base(args...)
        , m_Owner(hlp::Param<const boost::shared_ptr<Owner>>::Unpack(args...))
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
        boost::system::error_code e;
        const auto bytes = Base::m_IoObject->read_some(buffer, e);
        Base::m_IoObject->get_io_service().post(
                boost::bind(&Base::ReadMessageCallback, Base::shared_from_this(), e, bytes));
    }


    virtual void Write(const MemHolder& holder) override
    {
        boost::system::error_code e;
        const auto written = boost::asio::write(*Base::m_IoObject,
                                                boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                boost::asio::transfer_all(),
                                                e);
        Base::WriteCallback(e, written, holder);
    }

    virtual void ConnectionClosed() override
    {
        if (const auto owner = m_Owner.lock())
            owner->ConnectionClosed(Base::shared_from_this());
        if (Base::m_Callback)
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
