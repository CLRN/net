#pragma once

#include "net/connection.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/cstdint.hpp>
#include <boost/asio/strand.hpp>
#include <boost/function.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace net
{
namespace details
{

//! Channel members holder
template<typename Traits>
class BaseChannel : public IConnection, public boost::enable_shared_from_this<BaseChannel<Traits>>
{
public:
    typedef boost::asio::mutable_buffers_1 Buffer;
    typedef boost::weak_ptr<typename Traits::Transport> OwnerPtr;

    template<typename ... Args>
    BaseChannel(const Args& ... args)
        : m_IoObject(hlp::Param<const typename Traits::Handle>::Unpack(args...))
        , m_Owner(hlp::Param<const boost::shared_ptr<typename Traits::Transport>>::Unpack(args...))
    {
    }

    virtual void Read(const Buffer& buffer) = 0;
    virtual void Write(const MemHolder& holder) = 0;

    virtual void ConnectionClosed()
    {
        if (const auto owner = m_Owner.lock())
            owner->ConnectionClosed(this->shared_from_this());
        if (m_Callback)
            m_Callback(IConnection::StreamPtr());
    }

    typename Traits::Handle GetHandle() const
    {
        return m_IoObject;
    }

    //! Increment offset, method is virtual because some random access io objects must have access to bytes read
    virtual void OnBytesRead(const std::size_t /*bytes*/)
    {
    }

protected:
    const OwnerPtr m_Owner;
    const typename Traits::Handle m_IoObject;
    IConnection::Callback m_Callback;
};

} // namespace details
} // namespace net
