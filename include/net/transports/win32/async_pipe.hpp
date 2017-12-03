#pragma once

#include <string>
#include <set>

#include <boost/asio/io_service.hpp>
#include <boost/asio/windows/stream_handle.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/weak_ptr.hpp>

namespace net
{
namespace pipes
{

namespace details
{
    class ClientConnectOperation;
} // namespace details

//! Pipe acceptor/connector
class Pipe : boost::noncopyable, public boost::enable_shared_from_this<Pipe>
{
public:

	//! Pointer to the pipe handle
    typedef boost::shared_ptr<boost::asio::windows::stream_handle> Ptr;

    //! Connect callback
    typedef boost::function<void(const Ptr& pipe, const boost::system::error_code& e)> CallbackFn;

	//! Pipe message type
	struct Type
	{
		enum Value
		{
			Stream		= 0,
			Message		= 1,
			Default		= Stream
		};
	};

    inline Pipe(const std::string& pipeName, boost::asio::io_service& service, const std::size_t bufferSize, const Type::Value type = Type::Default);
    inline ~Pipe();

    inline void AsyncAccept(const CallbackFn& handler);
    inline void Accept(Ptr& client);
    inline void Accept(Ptr& client, boost::system::error_code& error);
    inline void Connect(Ptr& server);
    inline void Connect(Ptr& server, boost::system::error_code& error);
    inline void Close();

    inline HANDLE CreatePipe(boost::system::error_code& e);
    inline HANDLE ConnectToPipe(boost::system::error_code& e);

private:

    inline void OnOperationCompleted(details::ClientConnectOperation* op, const boost::system::error_code& e, const CallbackFn& cb, bool destroyed);

private:
	const std::string m_PipeName;
	boost::asio::io_service& m_Service;
    const std::size_t m_BufferSize;
	const Type::Value m_Type;
    std::set<details::ClientConnectOperation*> m_Pipes;
    boost::mutex m_Mutex;
};

} // namespace pipes
} // namespace net

#include "async_pipe_impl.hpp"

