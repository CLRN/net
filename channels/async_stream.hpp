#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Owner>
class AsyncStream : public net::details::Channel<typename Owner::Handle,
                                                typename Owner::Queue,
                                                typename Owner::Settings>
{
public:
    typedef details::Channel<typename Owner::Handle,
                             typename Owner::Queue,
                             typename Owner::Settings> Base;
    typedef boost::weak_ptr<Owner> OwnerPtr;

    template<typename ... Args>
    AsyncStream(const Args&... args)
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
        Base::m_IoObject->async_read_some(
            buffer,
            boost::bind(&Base::ReadMessageCallback, Base::shared_from_this(), _1, _2)
        );   
    }


    virtual void Write(const MemHolder& holder) override
    {
        assert(*reinterpret_cast<const boost::uint32_t*>(holder.m_Memory.get()) ==
               holder.m_Size - sizeof(boost::uint32_t));

        boost::asio::async_write(
                *Base::m_IoObject,
                boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                boost::asio::transfer_exactly(holder.m_Size),
                Base::m_Strand.wrap(boost::bind(&AsyncStream::WriteCallback, Base::shared_from_this(), _1, _2, holder))
        );
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
