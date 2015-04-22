#pragma once

#include "net/connection.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/cstdint.hpp>
#include <boost/asio/strand.hpp>
#include <boost/function.hpp>
#include <boost/asio/buffer.hpp>

namespace net
{
namespace details
{

//! Channel members holder
template<typename Handle, typename Queue>
class BaseChannel : public IConnection
{
public:
    typedef boost::asio::mutable_buffers_1 Buffer;

    virtual void Read(const Buffer& buffer) = 0;
    virtual void Write(const MemHolder& holder) = 0;
    virtual void ConnectionClosed() = 0;

    BaseChannel(const Handle& h) : m_IoObject(h)
    {
    }

protected:
    const Handle m_IoObject;
};

} // namespace details
} // namespace net
