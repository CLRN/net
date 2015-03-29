#pragma once

#include "allocator.hpp"

#include <vector>

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/asio/buffer.hpp>

namespace net
{

//! Remote host connection abstraction
template<typename Allocator>
class IConnection : boost::noncopyable
{
public:
    virtual ~IConnection(){}

    //! Pointer type
    typedef boost::shared_ptr<IConnection> Ptr;

    //! Data pointer type
    typedef boost::shared_ptr<std::istream> StreamPtr;

    //! Callback function type
    typedef boost::function<void (const StreamPtr& stream)> Callback;

    //! Prepare data buffer
    virtual typename Allocator::Memory Prepare(std::size_t size) = 0;

    //! Send to remote host
    virtual void Send(const typename Allocator::Memory& data, std::size_t size) = 0;

    //! Receive callback
    virtual void Receive(const Callback& callback) = 0;

    //! Flush data synchronously
    virtual void Flush() = 0;

    //! Close connection
    virtual void Close() = 0;
};


} // namespace net

