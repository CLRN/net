#pragma once

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
template<typename Handle, typename Allocator, typename Queue>
class BaseChannel : public IConnection<Allocator>
{
public:
    typedef boost::asio::mutable_buffers_1 Buffer;
    typedef typename Allocator::MemHolder MemHolder;

    virtual void Read(const Buffer& buffer) = 0;
    virtual void Write(const MemHolder& holder) = 0;
    virtual void ConnectionClosed(const boost::system::error_code& e, std::size_t bytes) = 0;

    BaseChannel(const Handle& h) : m_IoObject(h)
    {
    }

protected:
    const Handle m_IoObject;
};

} // namespace details
} // namespace net