#pragma once

#include <memory>
#include <functional>

#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

namespace net
{
namespace details
{
    class IData;
} // namespace details

//! Remote host connection abstraction
class IConnection : boost::noncopyable
{
public:
    virtual ~IConnection(){}

    //! Pointer type
    typedef boost::shared_ptr<IConnection> Ptr;

    //! Data pointer type
    typedef boost::shared_ptr<std::istream> StreamPtr;

    //! Callback function type
    typedef std::function<void (const StreamPtr& stream)> Callback;

    //! Allocate data block which is going to be send through connection after block handler has been destroyed
    virtual boost::shared_ptr<net::details::IData> Prepare(std::size_t size) = 0;

    //! Receive callback
    virtual void Receive(const Callback& callback) = 0;

    //! Flush data synchronously
    virtual void Flush() = 0;

    //! Close connection
    virtual void Close() = 0;

    //! Get connection information
    virtual std::string GetInfo() const = 0;
};

} // namespace net

