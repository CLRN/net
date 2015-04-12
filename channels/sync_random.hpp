#pragma once

#include "net/details/channel_base.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Owner>
class SyncRandom : public net::details::Channel<typename Owner::Handle,
                                                typename Owner::Queue,
                                                typename Owner::Settings>
{
public:
    typedef details::Channel<typename Owner::Handle,
                             typename Owner::Queue,
                             typename Owner::Settings> Base;
    typedef boost::weak_ptr<Owner> OwnerPtr;


    template<typename ... Args>
    SyncRandom(Args... args)
        : Base(args...)
        , m_Owner(hlp::Param<OwnerPtr>::Unpack(args...))
        , m_WriteOffset()
        , m_ReadOffset()
    {
    }

    //! Begin asynchronous read to buffer
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        boost::asio::async_read_at
        (
            *Base::m_IoObject,
            m_ReadOffset,
            buffer,
            boost::bind(&Base::ReadMessageCallback, this, _1, _2)
        );     
    }

    virtual void Write(const typename Base::MemHolder& holder) override
    {
        boost::system::error_code e;
        const auto written = boost::asio::write_at(*Base::m_IoObject,
                                                   m_WriteOffset,
                                                   boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                   boost::asio::transfer_all(), e);
        if (written != holder.m_Size || e)
            ConnectionClosed(e, written);

        m_WriteOffset += written;
    }

    virtual void ConnectionClosed(const boost::system::error_code& e, std::size_t bytes) override
    {
        LOG_INFO("Connection closed, bytes: %s, error: (%s) %s", bytes, e.value(), e.message());
        if (const auto owner = m_Owner.lock())
            owner->ConnectionClosed(Base::shared_from_this(), e);
        this->m_Callback(IConnection::StreamPtr());
    }

    virtual void OnBytesRead(const std::size_t bytes) override
    {
        m_ReadOffset += bytes;
    }

private:
    const OwnerPtr m_Owner;
    boost::uint64_t m_ReadOffset;
    boost::uint64_t m_WriteOffset;
};

} // namespace channels
} // namespace net
