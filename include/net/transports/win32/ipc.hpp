#pragma once

#include "conversion/cast.h"
#include "log/log.h"

#include "net/details/params.hpp"
#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/factory.hpp"
#include "net/channels/traits.hpp"

#include <atomic>
#include <fstream>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/asio/windows/object_handle.hpp>

namespace net
{
namespace ipc
{


namespace details
{

enum { MAX_FILE_SIZE = 1024 * 1024 * 50 };

inline void GetValidSharedFile(const char* filename, std::string& dir)
{
    static boost::once_flag flag;
    boost::call_once([filename, &dir]()
    {
        boost::interprocess::ipcdetail::create_shared_dir_cleaning_old_and_get_filepath(filename, dir);
    }, flag);

    if (dir.empty())
        boost::interprocess::ipcdetail::shared_filepath(filename, dir);
}


class InterprocessCondition
{
public:
    InterprocessCondition(boost::asio::io_service& svc) : m_Event(svc)
    {

    }

    InterprocessCondition(boost::asio::io_service& svc, const std::string& name) : m_Event(svc)
    {
        Open(name);
    }

    ~InterprocessCondition()
    {
        Close();
    }

    void Close()
    {
        if (m_Event.is_open())
        {
            Signal();
            m_Event.close();
        }
    }

    void Open(const std::string& name)
    {
        assert(m_Name.empty());

        m_Name = name;
        const auto handle = CreateEventA(NULL, FALSE, FALSE, m_Name.c_str());
        if (!m_Event.native_handle())
            BOOST_THROW_EXCEPTION(Exception("Failed to create event") << SysErrorInfo(boost::system::error_code(GetLastError(), boost::system::system_category())));
        m_Event.assign(handle);
    }

    void Signal()
    {
        if (!SetEvent(m_Event.native_handle()))
            BOOST_THROW_EXCEPTION(Exception("Failed to set event") << SysErrorInfo(boost::system::error_code(GetLastError(), boost::system::system_category())));
    }
    
    template<typename T>
    void Wait(boost::asio::io_service& svc, T callback)
    {
        m_Event.async_wait([callback](const boost::system::error_code&)
        {
            callback();
        });
    }

private:
    std::string m_Name;
    boost::asio::windows::object_handle m_Event;
};


class FileQueue : public boost::enable_shared_from_this<FileQueue>
{
    struct Header
    {
        boost::uint32_t m_Eof;
        boost::uint32_t m_NextFile;
    };

public:

    enum class Mode
    {
        Server = 0,
        Client = 1
    };

    typedef boost::function<void(boost::system::error_code, std::size_t)> Callback;

    FileQueue(boost::asio::io_service& svc, const std::string& endpoint) 
        : m_Service(svc)
        , m_Mode(Mode::Server)
        , m_Endpoint(endpoint)
        , m_IsClosed()
        , m_SinkIndex(0)
        , m_SourceIndex(0)
        , m_ReadOffset(sizeof(Header))
        , m_WriteOffset(sizeof(Header))
        , m_ConnectCondition(svc)
        , m_OugoingWriteCondition(svc)
        , m_IncomingWriteCondition(svc)
    {
    }

    ~FileQueue()
    {
        boost::system::error_code e;
        close(e);
    }

    const std::string& GetName() const { return m_Endpoint; }

    void Create(const boost::function<void(const boost::exception_ptr& e)>& callback)
    {
        m_Mode = Mode::Server;
        m_ConnectCondition.Open(m_Endpoint + "_connect");
        m_OugoingWriteCondition.Open(m_Endpoint + "_server_write_cond");
        m_IncomingWriteCondition.Open(m_Endpoint + "_client_write_cond");

        GetValidSharedFile((m_Endpoint + "_client_alive").c_str(), m_RemoteAliveLockName);
        GetValidSharedFile((m_Endpoint + "_server_alive").c_str(), m_LocalAliveLockName);
        GetValidSharedFile((m_Endpoint + "_connect").c_str(), m_ConnectLockName);

        {
            std::ofstream s1(m_LocalAliveLockName, std::ios::out);
            std::ofstream s2(m_RemoteAliveLockName, std::ios::out);
            std::ofstream s3(m_ConnectLockName, std::ios::out);
        }
        
        m_AliveLockFile = boost::interprocess::file_lock(m_LocalAliveLockName.c_str());
        m_RemoteAliveLockFile = boost::interprocess::file_lock(m_RemoteAliveLockName.c_str());
        m_ConnectLock = boost::interprocess::file_lock(m_ConnectLockName.c_str());

        m_AliveLock = boost::interprocess::scoped_lock<boost::interprocess::file_lock>(m_AliveLockFile);

        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_ConnectLock);

        OpenSink();

        const auto instance(shared_from_this());
        m_ConnectCondition.Wait(m_Service, [this, instance, callback]()
        {
            try
            {
                OpenSource();
                callback(boost::exception_ptr());
            }
            catch (const std::exception&)
            {
                callback(boost::current_exception());
            }
        });
    }

    void Connect()
    {
        m_Mode = Mode::Client;

        m_ConnectCondition.Open(m_Endpoint + "_connect");
        m_OugoingWriteCondition.Open(m_Endpoint + "_client_write_cond");
        m_IncomingWriteCondition.Open(m_Endpoint + "_server_write_cond");

        GetValidSharedFile((m_Endpoint + "_server_alive").c_str(), m_RemoteAliveLockName);
        GetValidSharedFile((m_Endpoint + "_client_alive").c_str(), m_LocalAliveLockName);
        GetValidSharedFile((m_Endpoint + "_connect").c_str(), m_ConnectLockName);

        m_AliveLockFile = boost::interprocess::file_lock(m_LocalAliveLockName.c_str());
        m_RemoteAliveLockFile = boost::interprocess::file_lock(m_RemoteAliveLockName.c_str());
        m_ConnectLock = boost::interprocess::file_lock(m_ConnectLockName.c_str());
        m_AliveLock = boost::interprocess::scoped_lock<boost::interprocess::file_lock>(m_AliveLockFile);

        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_ConnectLock);
        OpenSink();
        OpenSource();

        m_ConnectCondition.Signal();
    }

    void Read(void* ptr, std::size_t size, const Callback& cb)
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        if (m_IsClosed)
            return;

        if (m_SourceHeader.m_Eof == m_ReadOffset)
        {
            boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_SourceHeaderLock);
            ReadSourceHeader();
        }

        const boost::weak_ptr<FileQueue> instance(shared_from_this());
        if (m_SourceHeader.m_Eof != m_ReadOffset)
        {
            m_Service.post([this, instance, ptr, size, cb]()
            {
                if (const auto lock = instance.lock())
                    ReadAndIvokeCallback(reinterpret_cast<char*>(ptr), size, cb);
            });
        }
        else
        {
            m_IncomingWriteCondition.Wait(m_Service, [this, instance, ptr, size, cb]()
            {
                if (const auto lock = instance.lock())
                    ReadAndIvokeCallback(reinterpret_cast<char*>(ptr), size, cb);
            });
        }
    }

    void Write(const void* ptr, std::size_t size)
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        if (m_IsClosed)
            return;

        const char* data = reinterpret_cast<const char*>(ptr);
        while (size)
        {
            const auto bytes = std::min(size, static_cast<std::size_t>(MAX_FILE_SIZE - m_SinkHeader.m_Eof));
            memcpy(m_Sink.data() + m_SinkHeader.m_Eof, data, bytes);
            m_SinkHeader.m_Eof += bytes;
            size -= bytes;
            data += bytes;

            if (MAX_FILE_SIZE == m_SinkHeader.m_Eof)
            {
                // create next file
                ++m_SinkIndex;
                m_SinkHeader.m_NextFile = m_SinkIndex;
                UpdateSinkHeader();
                OpenSink();
            }
        }

        UpdateSinkHeader();
    }

    void UpdateSinkHeader()
    {
        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_SinkHeaderLock);
        reinterpret_cast<Header&>(*m_Sink.data()) = m_SinkHeader;

        m_OugoingWriteCondition.Signal();
    }

    void close(const boost::system::error_code& e = boost::system::error_code())
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        if (m_IsClosed)
            return;

        m_IsClosed = true;
        m_AliveLock.unlock();

        m_ConnectCondition.Close();
        m_OugoingWriteCondition.Close();
        m_IncomingWriteCondition.Close();

        m_Sink.close();
        boost::interprocess::file_lock().swap(m_SinkHeaderLock);
        boost::interprocess::file_lock().swap(m_SourceHeaderLock);
        boost::interprocess::file_lock().swap(m_AliveLockFile);
        boost::interprocess::file_lock().swap(m_RemoteAliveLockFile);
    }

    bool is_open()
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        return !m_IsClosed;
    }

private:

    std::string GetSourceName()
    {
        return m_Endpoint + boost::lexical_cast<std::string>(m_SourceIndex) + (m_Mode == Mode::Server ? "_cli_sink" : "_srv_sink");
    }

    void OpenSink()
    {
        m_Sink.close();

        const auto name = m_Endpoint + boost::lexical_cast<std::string>(m_SinkIndex) + (m_Mode == Mode::Server ? "_srv_sink" : "_cli_sink");

        std::string sinkLockName;
        GetValidSharedFile((name + "_lock").c_str(), sinkLockName);

        std::string fileName;
        GetValidSharedFile(name.c_str(), fileName);

        boost::iostreams::mapped_file_params params;
        params.path = fileName;
        params.offset = 0;
        params.length = MAX_FILE_SIZE;
        params.new_file_size = MAX_FILE_SIZE;

        if (boost::filesystem::exists(fileName))
            boost::filesystem::remove(fileName);

        m_Sink.open(params);

        // init locks
        {
            std::ofstream s1(sinkLockName, std::ios::out);
        }

        m_SinkHeaderLock = boost::interprocess::file_lock(sinkLockName.c_str());

        // set up sink offsets
        m_SinkHeader.m_Eof = sizeof(Header);
        m_SinkHeader.m_NextFile = 0;

        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_SinkHeaderLock);
        reinterpret_cast<Header&>(*m_Sink.data()) = m_SinkHeader;
    }

    void OpenSource(const std::size_t index = 0)
    {
        m_SourceIndex = index;
        if (!m_SourceName.empty())
        {
            boost::system::error_code e;
            boost::interprocess::file_lock().swap(m_SourceHeaderLock);
            const auto sourceLockName = m_SourceName + "_lock";

            boost::filesystem::remove(m_SourceName, e);
            boost::filesystem::remove(sourceLockName, e);
        }

        m_SourceName = GetSourceName();

        std::string sourceLockName;
        GetValidSharedFile((m_SourceName + "_lock").c_str(), sourceLockName);

        // init locks
        {
            std::ofstream s1(sourceLockName, std::ios::out);
        }

        m_SourceHeaderLock = boost::interprocess::file_lock(sourceLockName.c_str());
        m_ReadOffset = sizeof(Header);

        std::string fileName;
        GetValidSharedFile(m_SourceName.c_str(), fileName);
        m_SourceMapping = std::make_unique<boost::interprocess::file_mapping>(fileName.c_str(), boost::interprocess::read_only);

        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_SourceHeaderLock);
        ReadSourceHeader();
    }

    void WriteSourceHeader()
    {
        boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_SinkHeaderLock);
        reinterpret_cast<Header&>(*m_Sink.data()) = m_SinkHeader;
    }

    void ReadSourceHeader()
    {
        boost::interprocess::mapped_region region(*m_SourceMapping, boost::interprocess::read_only, 0, sizeof(Header));
        m_SourceHeader = *reinterpret_cast<const Header*>(region.get_address());
    }

    void ReadAndIvokeCallback(char* buffer, const std::size_t size, const Callback& cb)
    {
        try
        {
            if (m_RemoteAliveLockFile.try_lock())
                throw std::runtime_error("remote side is dead");

            if (m_SourceHeader.m_Eof == m_ReadOffset)
            {
                boost::interprocess::scoped_lock<boost::interprocess::file_lock> lock(m_SourceHeaderLock);
                ReadSourceHeader();
            }

            if (m_SourceHeader.m_Eof == m_ReadOffset)
            {
                Read(buffer, size, cb);
                return;
            }

            const auto bytes = std::min(std::size_t(m_SourceHeader.m_Eof - m_ReadOffset), size);
            boost::interprocess::mapped_region region(*m_SourceMapping, boost::interprocess::read_only, m_ReadOffset, bytes);
            memcpy(buffer, reinterpret_cast<const char*>(region.get_address()), bytes);
            m_ReadOffset += bytes;

            if (m_ReadOffset == m_SourceHeader.m_Eof && m_SourceHeader.m_NextFile)
                OpenSource(m_SourceHeader.m_NextFile);

            try
            {
                cb(boost::system::error_code(), bytes);
            }
            catch (const std::exception&)
            {
                assert(false);
            }
        }
        catch (const std::exception&)
        {
            try
            {
                cb(boost::system::error_code(boost::asio::error::eof), 0);
            }
            catch (const std::exception&)
            {
                assert(false);
            }

            close();
        }
    }

    bool IsRemoteAlive()
    {
        try
        {
            return !m_RemoteAliveLockFile.try_lock();
        }
        catch (const boost::interprocess::interprocess_exception&)
        {
            return false;
        }
    }

private:
    boost::asio::io_service& m_Service;
    Mode m_Mode;
    const std::string m_Endpoint;
    std::string m_RemoteAliveLockName;
    std::string m_LocalAliveLockName;
    std::string m_ConnectLockName;

    std::size_t m_SinkIndex;
    std::size_t m_SourceIndex;

    std::atomic<bool> m_IsClosed;

    boost::iostreams::mapped_file_sink m_Sink;
    std::string m_SourceName;
    std::unique_ptr<boost::interprocess::file_mapping> m_SourceMapping;

    Header m_SinkHeader;
    Header m_SourceHeader;

    boost::uint64_t m_ReadOffset;
    boost::uint64_t m_WriteOffset;

    boost::interprocess::file_lock m_SinkHeaderLock;
    boost::interprocess::file_lock m_SourceHeaderLock;
    boost::interprocess::file_lock m_ConnectLock;

    boost::mutex m_Mutex;

    InterprocessCondition m_ConnectCondition;
    InterprocessCondition m_OugoingWriteCondition;
    InterprocessCondition m_IncomingWriteCondition;

    boost::interprocess::file_lock m_AliveLockFile;
    boost::interprocess::file_lock m_RemoteAliveLockFile;
    boost::interprocess::scoped_lock<boost::interprocess::file_lock> m_AliveLock;
};

} // namespace details





template
<
    template<typename> class Channel,
    template<typename> class QueueImpl,
    typename Header = details::DefaultHeader,
    typename Settings = DefaultSettings
>
class Transport : public boost::enable_shared_from_this<Transport<Channel, QueueImpl, Header, Settings>>
{
    typedef boost::shared_ptr<details::FileQueue> Handle;
    typedef QueueImpl<Settings> Queue;
    typedef Transport<Channel, QueueImpl, Header, Settings> ThisType;
    typedef boost::enable_shared_from_this<ThisType> Shared;
    typedef net::details::ChannelTraits<Handle, Queue, Settings, ThisType, Header> Traits;

public:
    typedef boost::shared_ptr<Transport> Ptr;
    typedef Channel<Traits> ChannelImpl;

private:
    class QueueChannel : public ChannelImpl
    {
    public:
        template<typename ... Args>
        QueueChannel(const Args&... args) : ChannelImpl(args...) 
        {
        }

        virtual std::string GetInfo() const override
        {
            return ChannelImpl::m_IoObject->GetName();
        }

        virtual void Close() override
        {
            m_IoObject->close();
        }
    };

public:
    template<typename ... Args>
    Transport(const Args&... args)
        : m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Factory(::net::details::MakeFactory<QueueChannel, Handle, Ptr>(args..., ::net::details::ByteOrder::Host))
    {
    }

    ~Transport()
    {
        Close();
    }

    void Receive(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        m_ClientConnectedCallback = callback;
        const auto queue = boost::make_shared<details::FileQueue>(m_Service, endpoint);
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            m_Channels.emplace_back(queue);
        }

        const auto connection = m_Factory->Create(queue, Shared::shared_from_this());
        const boost::weak_ptr<Transport<Channel, QueueImpl, Header, Settings>> instance(Shared::shared_from_this());
        queue->Create([this, instance, queue, connection](const boost::exception_ptr& e)
        {
            if (const auto locked = instance.lock())
                m_ClientConnectedCallback(connection, e);
        });
    }

    void ConnectionClosed(const IConnection::Ptr& connection)
    {
        if (m_ClientConnectedCallback)
            m_ClientConnectedCallback(connection, boost::current_exception());
    }

    void Connect(const ITransport::Endpoint& endpoint, const ITransport::Callback& callback)
    {
        const auto queue = boost::make_shared<details::FileQueue>(m_Service, endpoint);
        queue->Connect();

        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            m_Channels.emplace_back(queue);
        }

        callback(m_Factory->Create(queue, Shared::shared_from_this()), boost::exception_ptr());
    }

    void Close()
    {
        std::vector<boost::weak_ptr<details::FileQueue>> local;
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            m_Channels.swap(local);
        }

        boost::system::error_code e;
        for (const auto& channel : local)
        {
            if (const auto locked = channel.lock())
                locked->close(e);
        }
    }

private:

private:
    boost::asio::io_service& m_Service;
    ITransport::Callback m_ClientConnectedCallback;
    typename net::details::IFactory<QueueChannel, Handle, Ptr>::Ptr m_Factory;
    std::vector<boost::weak_ptr<details::FileQueue>> m_Channels;
    boost::mutex m_Mutex;
};

} // namespace ipc
} // namespace net
