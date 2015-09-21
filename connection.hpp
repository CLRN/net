#pragma once

#include "details/memory.hpp"

#include <memory>
#include <functional>

#include <boost/noncopyable.hpp>

namespace net
{

//! Remote host connection abstraction
class IConnection : boost::noncopyable
{
public:
    virtual ~IConnection(){}

    //! Pointer type
    typedef boost::shared_ptr<IConnection> Ptr;

    //! Data pointer type
    typedef std::unique_ptr<std::istream> StreamPtr;

    //! Callback function type
    typedef std::function<void (StreamPtr&& stream)> Callback;

    //! Prepare data buffer
    virtual Memory Prepare(std::size_t size) = 0;

    //! Send to remote host
    virtual void Send(const Memory& data, std::size_t size) = 0;

    //! Receive callback
    virtual void Receive(const Callback& callback) = 0;

    //! Flush data synchronously
    virtual void Flush() = 0;

    //! Close connection
    virtual void Close() = 0;

    //! Get connection information
    virtual std::string GetInfo() = 0;
};


} // namespace net

