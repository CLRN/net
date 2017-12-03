#pragma once

#include "net/connection.hpp"
#include "params.hpp"

#include <atomic>

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/cstdint.hpp>
#include <boost/asio/strand.hpp>
#include <boost/function.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace net
{
namespace details
{

enum class Direction
{
    Incoming = 0,
    Outgoing = 1
};

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
        , m_IsDisconnectReported()
        , m_Direction(hlp::Param<Direction>::Unpack(args..., Direction::Incoming))
        , m_IsTerminating()
    {
    }

    virtual void DoRead(const Buffer& buffer) = 0;
    virtual void DoWrite(const std::vector<boost::asio::const_buffer>& buffers) = 0;

    virtual void ConnectionClosed()
    {
        if (!Traits::Settings::IsPersistent() || IsTerminating())
        {
            bool alreadyClosed = false;
            if (!m_IsDisconnectReported.compare_exchange_strong(alreadyClosed, true))
                return; // no need to invoke callback twice

            Close();

            if (m_Callback)
                m_Callback(IConnection::StreamPtr());
        }

        if (const auto owner = m_Owner.lock())
            owner->ConnectionClosed(boost::dynamic_pointer_cast<IConnection>(this->shared_from_this()));
    }

    typename Traits::Handle GetHandle() const
    {
        return m_IoObject;
    }

    //! Increment offset, method is virtual because some random access io objects must have access to bytes read
    virtual void OnBytesRead(const std::size_t /*bytes*/)
    {
    }

    Direction GetDirection() const
    {
        return m_Direction;
    }

    bool IsTerminating() const
    {
        return m_IsTerminating;
    }

protected:
    const OwnerPtr m_Owner;
    const Direction m_Direction;
    typename Traits::Handle m_IoObject;
    IConnection::Callback m_Callback;
    std::atomic<bool> m_IsDisconnectReported;
    std::atomic<bool> m_IsTerminating;
};

} // namespace details
} // namespace net
