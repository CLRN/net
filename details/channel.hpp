#pragma once

#include "stream.hpp"
#include "channel_base.hpp"

#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/cstdint.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/iostreams/stream.hpp>

namespace net
{
namespace details
{

//! Pipe/file channel abstraction
template
<
        typename Handle,
        typename Allocator,
        typename Queue,
        typename Settings
>
class Channel 
    : public boost::enable_shared_from_this<Channel<Handle, Allocator, Queue, Settings> >
    , public BaseChannel<Handle, Allocator, Queue>
{
    typedef typename Allocator::Memory Memory;
    typedef typename IConnection<Allocator>::Callback Callback;
    typedef typename Allocator::MemHolder MemHolder;
    typedef BaseChannel<Handle, Allocator, Queue> Base;

public:
    typedef boost::shared_ptr<Channel<Handle, Allocator, Queue, Settings>> Ptr;

protected:
    Channel(boost::asio::io_service& svc, const Handle& h)
        : Base(h)
        , m_Service(svc)
        , m_Strand(svc)
        , m_MessageSize()
        , m_ReadBytes()
        , m_ParsedBytes()
        , m_Queue(boost::bind(&Base::Write, this, _1))
    {

    }

    virtual ~Channel()
    {
        Flush();
        Close();
    }

    virtual void Close() override
    {
        boost::system::error_code e;
        Base::m_IoObject->close(e);
    }

    virtual void Flush() override
    {
        while (!m_Queue.IsEmpty() && !m_Service.stopped())
        {
            if (!m_Service.poll_one())
                boost::this_thread::sleep(boost::posix_time::milliseconds(1));
        }
    }

    virtual void Send(const Memory& data, std::size_t size) override
    {
        MemHolder holder = 
        { 
            typename Allocator::Memory(data.get() - sizeof(boost::uint32_t), [data](char*){}), 
            size + sizeof(boost::uint32_t) 
        };

        reinterpret_cast<boost::uint32_t&>(*holder.m_Memory.get()) = size;
        m_Queue.Push(std::move(holder));
    }

    virtual void Receive(const Callback& callback) override
    {
        m_Callback = callback;
        PrepareBuffer();
        const auto bufferSize = Settings::GetBufferSize();
        this->Read(boost::asio::buffer(&m_ReadBuffer[m_ReadBytes], bufferSize - m_ReadBytes));
    }

    virtual Memory Prepare(std::size_t size) override
    {
        const auto mem = Allocator::Allocate(size + sizeof(boost::uint32_t));
        return typename Allocator::Memory(mem.get() + sizeof(boost::uint32_t), [mem](char*){});
    }

    void WriteCallback(const boost::system::error_code& e, const std::size_t bytes, MemHolder)
    {
        if (e)
        {
            Base::ConnectionClosed(e, bytes);
            m_Queue.Clear();
        }
        else
        {
            m_Queue.Pop();
        }
    }

public:
    //! Read callback
    void ReadMessageCallback(boost::system::error_code e, const std::size_t bytes)
    {
        // increment write offset
        OnBytesRead(bytes);

        // commit received bytes to the input sequence
        m_ReadBytes += bytes;

        // parse incoming data
        Parse(e);

        if (e)
        {
            this->ConnectionClosed(e, bytes);
        }
        else
        {
            PrepareBuffer();
            this->Read(boost::asio::buffer(&m_ReadBuffer[m_ReadBytes], m_BufferSize - m_ReadBytes));
        }
    }

private:

    //! Copy not parsed data to new buffer
    void PrepareBuffer()
    {
        if (!m_ReadBuffer)
        {
            m_ReadBuffer = Allocator::Allocate(m_BufferSize);
            return;
        }

        // check for parsed bytes amount, if remaining buffer space 
        // less then 1/3 of initial space allocate new buffer
        assert(m_BufferSize >= m_ParsedBytes);

        const auto freeSpace = m_BufferSize - m_ReadBytes;
        if (freeSpace > m_BufferSize / 3)
            return; // we will read using old buffer

        // allocate new buffer, copy all unparsed data to new buffer
        const auto remainingBytes = m_ReadBytes - m_ParsedBytes;

        // copy remaining not parsed data to new buffer
        m_BufferSize = std::max(Settings::GetBufferSize(), m_MessageSize + sizeof(boost::uint32_t));
        auto buffer = Allocator::Allocate(m_BufferSize);
        memcpy(buffer.get(), &m_ReadBuffer[m_ParsedBytes], remainingBytes);

        m_ReadBuffer.swap(buffer);
        m_ReadBytes = remainingBytes;
        m_ParsedBytes = 0;
    }

    //! Parse incoming stream and invoke callbacks
    void Parse(boost::system::error_code& e)
    {
        const auto bytesFree = m_BufferSize - m_ReadBytes;
        for (; m_ParsedBytes != m_ReadBytes; )
        {
            // unparsed bytes amount
            const auto toParse = m_ReadBytes - m_ParsedBytes;
            if (toParse <= sizeof(boost::uint32_t))
            {
                // incomplete packet size
                break;
            }

            // obtain current message size
            const char* currentMessageBegin = &m_ReadBuffer[m_ParsedBytes];
            m_MessageSize = reinterpret_cast<const boost::uint32_t&>(*currentMessageBegin);
            if (m_MessageSize > Settings::GetMaxMessageSize()) // check size limit
            {
                // failed to read, probably connection is broken or client disconnected
                if (!e)
                    e = boost::system::error_code(boost::system::errc::value_too_large, boost::system::system_category());
                return;
            }

            // check for complete packet
            if (m_MessageSize <= toParse - sizeof(boost::uint32_t))
            {
                const char* data = &m_ReadBuffer[m_ParsedBytes + sizeof(boost::uint32_t)];
                InvokeCallback(data);
                m_ParsedBytes += (m_MessageSize + sizeof(boost::uint32_t));
            }
            else
            {
                // this message read partially
                break;
            }
        }
    }

    void InvokeCallback(const char* data)
    {
        try
        {
            // need to save link to memory buffer
            class StreamWithMemory : public boost::iostreams::stream<net::BinaryReadStream>
            {
            public:
                StreamWithMemory(const char* data, std::size_t size, const typename Allocator::Memory& mem) 
                    : boost::iostreams::stream<net::BinaryReadStream>(data, size)
                    , m_Memory(mem)
                {}
            private:
                const typename Allocator::Memory m_Memory;
            };

            const auto stream = boost::make_shared<StreamWithMemory>(data, m_MessageSize, m_ReadBuffer);

            // invoke callback
            m_Callback(stream);
        }
        catch (const std::exception&)
        {
            this->ConnectionClosed(boost::system::error_code(boost::system::errc::protocol_error,
                                                             boost::system::system_category()), 0);
        }
    }

    //! Increment offset, method is virtual because some random access io objects must have access to bytes read
    virtual void OnBytesRead(const std::size_t /*bytes*/)
    {
    }
protected:
    typename IConnection<Allocator>::Callback m_Callback;

private:
    boost::asio::io_service& m_Service;
    std::size_t m_BufferSize;
    std::size_t m_MessageSize;
    boost::uint32_t m_ReadBytes;
    boost::uint32_t m_ParsedBytes;
    mutable boost::mutex m_Mutex;
    boost::asio::io_service::strand m_Strand;
    typename Allocator::Memory m_ReadBuffer;
    Queue m_Queue;
};	

} // namespace details
} // namespace net
