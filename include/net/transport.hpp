#pragma once

#include "connection.hpp"

#include <string>

#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/exception/detail/exception_ptr.hpp>


namespace net
{

//! Transport abstraction
class ITransport
{
public:
    typedef std::string Endpoint;
    virtual ~ITransport(){}

    //! Pointer type
    typedef boost::shared_ptr<ITransport> Ptr;

    //! Callback function type
    typedef boost::function<void(const IConnection::Ptr& connection, const boost::exception_ptr& e)> Callback;

    //! Connect to remote host
    virtual void Connect(const Endpoint& endpoint, const Callback& callback) = 0;

    //! Start listening on specified endpoint
    virtual void Receive(const Endpoint& endpoint, const Callback& callback) = 0;

    //! Stop all activity
    virtual void Close() = 0;
};

} // namespace net
