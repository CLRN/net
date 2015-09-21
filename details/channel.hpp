#pragma once

#include "stream.hpp"
#include "channel_base.hpp"
#include "params.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/cstdint.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/make_unique.hpp>

namespace net
{
namespace details
{

//! Pipe/file channel abstraction
template<typename Traits>
class Channel : public BaseChannel<Traits>
{
    typedef BaseChannel<Traits> Base;
    typedef typename Traits::Settings Settings;
public:
    typedef boost::shared_ptr<Channel<Traits>> Ptr;

protected:

    template<typename ... Args>
    Channel(const Args&... args)
        : Base(args...)
        , m_Service(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_Strand(hlp::Param<boost::asio::io_service>::Unpack(args...))
        , m_MessageSize()
        , m_ReadBytes()
        , m_ParsedBytes()
        , m_Queue(args...)
        , m_Settings(hlp::Param<const Settings>::Unpack(args..., Settings()))
        , m_BufferSize(m_Settings.GetBufferSize())
    {

    }

    virtual ~Channel()
    {
        Flush();
        Close();
    }

    Ptr Shared()
    {
        return boost::dynamic_pointer_cast<Channel>(Base::shared_from_this());
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
            Memory(data.get() - sizeof(boost::uint32_t), [data](char*){}),
            size + sizeof(boost::uint32_t) 
        };

        reinterpret_cast<boost::uint32_t&>(*holder.m_Memory.get()) = size;
        m_Queue.Push(std::move(holder), boost::bind(&Base::Write, this, _1));
    }

    virtual void Receive(const IConnection::Callback& callback) override
    {
        Base::m_Callback = callback;
        PrepareBuffer();
        const auto bufferSize = m_Settings.GetBufferSize();
        this->Read(boost::asio::buffer(&m_ReadBuffer[m_ReadBytes], bufferSize - m_ReadBytes));
    }

    virtual Memory Prepare(std::size_t size) override
    {
        const auto mem = boost::make_shared_noinit<char[]>(size + sizeof(boost::uint32_t));
        return Memory(mem.get() + sizeof(boost::uint32_t), [mem](char*){});
    }

    void WriteCallback(const boost::system::error_code& e, const std::size_t bytes, MemHolder)
    {
        if (e)
        {
            try
            {
                BOOST_THROW_EXCEPTION(net::Disconnected("Write failed, bytes: %s", bytes) << net::SysErrorInfo(e));
            }
            catch (const std::exception&)
            {
                this->ConnectionClosed();
                Close();
            }

            m_Queue.Clear();
        }
        else
        {
            m_Queue.Pop(boost::bind(&Base::Write, this, _1));
        }
    }

public:
    //! Read callback
    void ReadMessageCallback(boost::system::error_code e, const std::size_t bytes)
    {
        // increment write offset
        Base::OnBytesRead(bytes);

        // commit received bytes to the input sequence
        m_ReadBytes += bytes;

        // parse incoming data
        Parse(e);

        if (e)
        {
            try
            {
                BOOST_THROW_EXCEPTION(net::Disconnected("Connection error") << net::SysErrorInfo(e));
            }
            catch (const net::Exception&)
            {
                this->ConnectionClosed();
                Close();
            }
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
            m_ReadBuffer = boost::make_shared_noinit<char[]>(m_BufferSize);
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
        m_BufferSize = std::max(m_Settings.GetBufferSize(), m_MessageSize + sizeof(boost::uint32_t));
        auto buffer = boost::make_shared_noinit<char[]>(m_BufferSize);
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
            if (m_MessageSize > m_Settings.GetMaxMessageSize()) // check size limit
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
                StreamWithMemory(const char* data, std::size_t size, const Memory& mem)
                    : boost::iostreams::stream<net::BinaryReadStream>(data, size)
                    , m_Memory(mem)
                {}
            private:
                const Memory m_Memory;
            };

            auto stream = boost::make_unique<StreamWithMemory>(data, m_MessageSize, m_ReadBuffer);

            // invoke callback
            Base::m_Callback(std::move(stream));
        }
        catch (const std::exception&)
        {
            this->ConnectionClosed();
            Close();
        }
    }

protected:
    boost::asio::io_service::strand m_Strand;

private:
    boost::asio::io_service& m_Service;
    typename Traits::Queue m_Queue;
    const Settings m_Settings;
    std::size_t m_BufferSize;
    std::size_t m_MessageSize;
    boost::uint32_t m_ReadBytes;
    boost::uint32_t m_ParsedBytes;
    mutable boost::mutex m_Mutex;
    Memory m_ReadBuffer;
};

} // namespace details
} // namespace net
