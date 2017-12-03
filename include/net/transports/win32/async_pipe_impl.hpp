#pragma once

#include "async_pipe.hpp"
#include "log/log.h"

#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>

#include "pipe_security.hpp"

namespace net
{
namespace pipes
{
namespace details
{

void ClientConnectedStub(boost::asio::detail::win_iocp_io_service* service, boost::asio::detail::operation* operation, const boost::system::error_code& e, std::size_t size);

class ClientConnectOperation : public boost::asio::detail::operation
{
public:
    typedef boost::shared_ptr<boost::asio::windows::stream_handle> PipePtr;
    typedef boost::function<void(ClientConnectOperation*, const boost::system::error_code&, bool)> CallbackFn;

    ClientConnectOperation(const HANDLE pipe, boost::asio::io_service& service, const CallbackFn& callback)
        : boost::asio::detail::operation(&ClientConnectedStub)
        , m_Pipe(new boost::asio::windows::stream_handle(service, pipe))
        , m_Callback(callback)
        , m_IsDestroyed()
    {
    }

    ~ClientConnectOperation()
    {
        m_Callback(this, m_Error, m_IsDestroyed);
    }
    void Do()
    {
        boost::asio::detail::win_iocp_io_service& iocpService = boost::asio::use_service<boost::asio::detail::win_iocp_io_service>(m_Pipe->get_io_service());
        iocpService.work_started();

        const BOOL result = ConnectNamedPipe(m_Pipe->native_handle(), this);
        const DWORD lastError = GetLastError();

        if (!result && lastError != ERROR_IO_PENDING)
            iocpService.on_completion(this, lastError);
        else
            iocpService.on_pending(this);
    }
    void Completed(const boost::system::error_code& e)
    {
        m_Error = e;
    }

    PipePtr GetPipe() const 
    { 
        return m_Pipe; 
    }

    void Destroy()
    {
        m_IsDestroyed = true;
    }


private:
    PipePtr m_Pipe;
    CallbackFn m_Callback;
    boost::system::error_code m_Error;
    bool m_IsDestroyed;
};

inline void ClientConnectedStub(boost::asio::detail::win_iocp_io_service* service, 
                                boost::asio::detail::operation* operation, 
                                const boost::system::error_code& e, 
                                std::size_t /*size*/)
{
    std::auto_ptr<ClientConnectOperation> pipeOperation(static_cast<ClientConnectOperation*>(operation));
    if (service)
        pipeOperation->Completed(e);
    else
        pipeOperation->Destroy();
}

} // namespace details

Pipe::Pipe(const std::string& pipeName, boost::asio::io_service& service, const std::size_t bufferSize, const Type::Value type /*= Type::Default*/)
    : m_PipeName(pipeName)
    , m_Service(service)
    , m_Type(type)
    , m_BufferSize(bufferSize)
{
}

Pipe::~Pipe()
{
    Close();
}

void Pipe::Close()
{
    boost::system::error_code e;

    boost::unique_lock<boost::mutex> lock(m_Mutex);
    for (auto& pipe : m_Pipes)
        pipe->GetPipe()->close(e);

    m_Pipes.clear();
}

HANDLE Pipe::ConnectToPipe(boost::system::error_code& e)
{
    HANDLE pipe = {};
    for (;;)
    {
        pipe = CreateFileA(m_PipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            NULL,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            const DWORD lastError = GetLastError();
            if (lastError == ERROR_PIPE_BUSY)
            {
                Sleep(1000);
            }
            else
            {
                e = boost::system::error_code(lastError, boost::asio::error::get_system_category());
                break;
            }
        }
        else
        {
            e = boost::system::error_code();
            break;
        }
    }

    return pipe;
}

HANDLE Pipe::CreatePipe(boost::system::error_code& e)
{
    const DWORD flags = m_Type == Type::Message 
        ? PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
        : PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;

    SECURITY_ATTRIBUTES sa;
    pipes::details::PipeSecurity psec;
    
    psec.BuildAttributes(sa);
    const HANDLE pipe = CreateNamedPipeA( 
        m_PipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | WRITE_DAC,
        flags,                
        PIPE_UNLIMITED_INSTANCES,
        static_cast<DWORD>(m_BufferSize),
        static_cast<DWORD>(m_BufferSize),
        NULL,
        &sa
    );

    const DWORD lastError = GetLastError();

    if (pipe == INVALID_HANDLE_VALUE)
    {
        e = boost::system::error_code(lastError, boost::asio::error::get_system_category());
        return NULL;
    }

    e = boost::system::error_code();
    return pipe;
}

void Pipe::Connect(Ptr& server, boost::system::error_code& error)
{
    assert(!server);
    const HANDLE pipe = ConnectToPipe(error);
    if (error)
        return;

    server.reset(new boost::asio::windows::stream_handle(m_Service, pipe));
}

void Pipe::Connect(Ptr& server)
{
    boost::system::error_code e;
    Connect(server, e);
    boost::asio::detail::throw_error(e, "connect");
}

void Pipe::Accept(Ptr& client, boost::system::error_code& error)
{
    assert(!client);
    const HANDLE pipe = CreatePipe(error);
    if (error)
        return;

    if (!ConnectNamedPipe(pipe, NULL))
        error = boost::system::error_code(GetLastError(), boost::asio::error::get_system_category());
    else
        client.reset(new boost::asio::windows::stream_handle(m_Service, pipe));
}

void Pipe::Accept(Ptr& client)
{
    boost::system::error_code e;
    Accept(client, e);
    boost::asio::detail::throw_error(e, "accept");
}

void Pipe::OnOperationCompleted(details::ClientConnectOperation* op, const boost::system::error_code& e, const CallbackFn& cb, bool destroyed)
{
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        m_Pipes.erase(op);
    }
    if (!destroyed)
        cb(op->GetPipe(), e);
}

void Pipe::AsyncAccept(const CallbackFn& handler)
{
    boost::system::error_code e;
    const HANDLE pipe = CreatePipe(e);
    boost::asio::detail::throw_error(e, "create");

    std::auto_ptr<details::ClientConnectOperation> operation(new details::ClientConnectOperation(
        pipe,
        m_Service,
        boost::bind(&Pipe::OnOperationCompleted, shared_from_this(), _1, _2, handler, _3)
    ));
    operation->Do();

    boost::unique_lock<boost::mutex> lock(m_Mutex);
    m_Pipes.insert(operation.release());
}


} // namespace pipes
} // namespace net