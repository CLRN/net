#pragma once

#include "stream.hpp"
#include "channel_base.hpp"
#include "params.hpp"
#include "net/exception.hpp"
#include "log/log.h"

#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/cstdint.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/make_unique.hpp>
#include <boost/make_shared.hpp>
#include <boost/crc.hpp>

namespace net
{
namespace details
{

#pragma pack(push, 1)

struct Header
{
    uint32_t m_Size;
    uint64_t m_BytesReceived;
};

struct RestorationHeader : public Header
{
    uint32_t m_Crc32;

    uint32_t Crc32() const
    {
        boost::crc_32_type crc;
        crc.process_bytes(&m_Size, sizeof(m_Size));
        crc.process_bytes(&m_BytesReceived, sizeof(m_BytesReceived));
        return crc.checksum();
    }
};

#pragma pack(pop)

SET_LOGGING_MODULE("Network");

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
        , m_Queue(boost::bind(&Base::DoWrite, this, _1), args...)
        , m_Settings(hlp::Param<const Settings>::Unpack(args..., Settings()))
        , m_BufferSize(m_Settings.GetBufferSize())
        , m_MinBufferSize()
        , m_ReceivedBytesLocal()
        , m_BytesWritten()
        , m_IsReadBufferBusy()
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
        Base::m_IsTerminating = true;
        while (Base::m_IoObject->is_open() && !m_Queue.IsEmpty() && !m_Service.stopped())
        {
            if (!m_Service.poll_one())
                boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
        }
    }

    virtual boost::shared_ptr<details::IData> Prepare(std::size_t size) override
    {
        Header header = {};
        header.m_Size = size;
        header.m_BytesReceived = m_ReceivedBytesLocal;

        const auto headerSize = sizeof(Header);
        const auto data = m_Queue.Prepare(size + headerSize);
        data->Write(&header, headerSize);
        return data;
    }

    virtual void Receive(const IConnection::Callback& callback) override
    {
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);

            const auto isReading = !!Base::m_Callback;
            Base::m_Callback = callback;
            if (isReading)
                return;
        }

        LockReadBuffer();
        PrepareBuffer();
        const auto bufferSize = m_Settings.GetBufferSize();
        this->DoRead(boost::asio::buffer(&m_ReadBuffer[m_ReadBytes], bufferSize - m_ReadBytes));
    }

    void WriteCallback(const boost::system::error_code& e, const std::size_t bytes, const typename Traits::Handle& handle)
    {
        const bool oldSocket = handle != Base::m_IoObject;
        if (oldSocket)
        {
            LOG_INFO("Ignoring bytes: %s, error: %s, written to old socket", bytes, e.message());
            return;
        }

        if (e)
        {
            LOG_ERROR("Write to %s failed, error: %s", this->GetInfo(), e.message());
            m_Queue.OnWriteFailed(bytes);

            boost::system::error_code ignore;
            handle->close(ignore);
        }
        else
        {
            m_Queue.OnWriteCompleted(bytes);
        }

        m_BytesWritten += bytes;
        if (!Settings::IsPersistent())
            m_Queue.UpdateAcknowledgedBytes(m_BytesWritten);
    }

public:

    Ptr Shared()
    {
        return boost::dynamic_pointer_cast<Channel>(Base::shared_from_this());
    }

    //! Read callback
    void ReadMessageCallback(boost::system::error_code e, std::size_t bytes, const typename Traits::Handle& handle)
    {
        const bool oldSocket = handle != Base::m_IoObject;
        if (oldSocket)
        {
            UnlockReadBuffer();

            LOG_INFO("Ignoring bytes: %s, error: %s, read from old socket", bytes, e.message());
            return;
        }

        // increment write offset
        Base::OnBytesRead(bytes);

        // commit received bytes to the input sequence
        m_ReadBytes += bytes;
        m_ReceivedBytesLocal += bytes;

        bool parsed = false;
        try
        {
            // parse incoming data
            Parse(e);
            parsed = true;

            if (e)
            {
                if (e != boost::asio::error::make_error_code(boost::asio::error::eof))
                    LOG_ERROR("Read from %s failed, error: %s", this->GetInfo(), e.message());
                else
                    LOG_DEBUG("Read from %s failed, error: %s", this->GetInfo(), e.message());

                m_Queue.OnFailure();

                boost::system::error_code ignore;
                handle->close(ignore);
                BOOST_THROW_EXCEPTION(net::Disconnected("Connection error") << net::SysErrorInfo(e));
            }
            else
            {
                PrepareBuffer();
                this->DoRead(boost::asio::buffer(&m_ReadBuffer[m_ReadBytes], m_BufferSize - m_ReadBytes));
            }
        }
        catch (const std::exception&)
        {
            if (!parsed)
            {
                LOG_ERROR("Closing connection %s because of exception, error: %s", this->GetInfo(), boost::current_exception_diagnostic_information());

                // drop this channel completely, probably channel state is invalid if we failed to parse the message
                Base::m_IsTerminating = true;
            }

            UnlockReadBuffer();
            Close();
            this->ConnectionClosed();
        }
    }

    //! Restore channel
    std::vector<char> RestoreChannel()
    {
        try
        {
            // send an empty packet so that we can synchronize bytes received
            RestorationHeader header = {};
            header.m_Size = 0;
            header.m_BytesReceived = m_ReceivedBytesLocal;
            header.m_Crc32 = header.Crc32();

            LOG_INFO("Restoring channel [%s], direction: %s, crc32: %s, local bytes: %s", this->GetInfo(), int(this->GetDirection()), header.m_Crc32, m_ReceivedBytesLocal);

            return std::vector<char>(reinterpret_cast<const char*>(&header), reinterpret_cast<const char*>(&header) + sizeof(RestorationHeader));

        }
        catch (const std::exception&)
        {
            LOG_ERROR("Failed to restore channel [%s], error: %s", this->GetInfo(), boost::current_exception_diagnostic_information(true));
            Close();
        }
        return std::vector<char>();
    }

    void HandleRestorationHeader(const RestorationHeader& header, boost::system::error_code e)
    {
        if (!e)
        {
            // this is a special case for channel restoration, update ack bytes and continue in normal mode
            const auto valid = header.Crc32() == header.m_Crc32;
            if (!valid)
            {
                LOG_ERROR("Invalid sync packet, crc from packet: %s, calculated: %s, bytes: %s", header.m_Crc32, header.Crc32(), header.m_BytesReceived);
                if (!e)
                    e = boost::system::error_code(boost::system::errc::protocol_error, boost::system::system_category());
                Base::m_IsTerminating = true; // do not reconnect this connection
                Close();
            }
            else
            {
                LOG_INFO("Received synchronization packet from [%s], remote bytes: %s, local: %s, direction: %s", this->GetInfo(), header.m_BytesReceived, m_ReceivedBytesLocal, int(this->GetDirection()));
                m_Queue.UpdateAcknowledgedBytes(header.m_BytesReceived);
                m_Queue.Reset();

                LockReadBuffer();
                PrepareBuffer();
                this->DoRead(boost::asio::buffer(&m_ReadBuffer[m_ReadBytes], m_BufferSize - m_ReadBytes));
            }
        }
        else
        {
            ReadMessageCallback(e, 0, Base::m_IoObject);
        }
    }

    //! Copy not parsed data to new buffer
    void PrepareBuffer()
    {
        try
        {
            if (!m_ReadBuffer)
            {
                m_ReadBuffer = boost::make_shared_noinit<char[]>(m_BufferSize);
                return;
            }

            // check for parsed bytes amount, if remaining buffer space 
            // less then 1/3 of initial space allocate new buffer
            assert(m_BufferSize >= m_ParsedBytes);
            assert(m_BufferSize >= m_ReadBytes);

            auto freeSpace = m_BufferSize - m_ReadBytes;
            if (freeSpace > m_BufferSize / 3 && (!m_MinBufferSize || freeSpace > m_MinBufferSize))
                return; // we will read using old buffer

            // allocate new buffer, copy all unparsed data to new buffer
            assert(m_ReadBytes >= m_ParsedBytes);
            const auto remainingBytes = m_ReadBytes - m_ParsedBytes;

            // copy remaining not parsed data to new buffer
            m_BufferSize = std::max(m_Settings.GetBufferSize(), m_MessageSize + sizeof(Header));
            assert(m_BufferSize > remainingBytes);
            freeSpace = m_BufferSize - remainingBytes;

            if (m_MinBufferSize && freeSpace < m_MinBufferSize)
            {
                assert(m_MinBufferSize > freeSpace);
                m_BufferSize += (m_MinBufferSize - freeSpace);
            }

            auto buffer = boost::make_shared_noinit<char[]>(m_BufferSize);
            memcpy(buffer.get(), &m_ReadBuffer[m_ParsedBytes], remainingBytes);

            m_ReadBuffer.swap(buffer);
            m_ReadBytes = remainingBytes;
            m_ParsedBytes = 0;
        }
        catch (...)
        {
            UnlockReadBuffer();
            throw;
        }
    }

    inline void LockReadBuffer()
    {
        bool locked = false;
        while (!m_IsReadBufferBusy.compare_exchange_weak(locked, true, std::memory_order_acquire))
        {
            locked = false;
            m_Service.poll_one();
        }
    }

    inline void UnlockReadBuffer()
    {
        m_IsReadBufferBusy.store(false, std::memory_order_release);
    }

private:

    //! Parse incoming stream and invoke callbacks
    void Parse(boost::system::error_code& e)
    {
        assert(m_BufferSize >= m_ReadBytes);
        const auto bytesFree = m_BufferSize - m_ReadBytes;
        for (; m_ParsedBytes != m_ReadBytes; )
        {
            // unparsed bytes amount
            assert(m_ReadBytes >= m_ParsedBytes);
            const auto toParse = m_ReadBytes - m_ParsedBytes;
            if (toParse <= sizeof(Header))
            {
                // incomplete packet size
                break;
            }

            if (m_ParsedBytes > m_BufferSize)
            {
                // broken packet
                if (!e)
                    e = boost::system::error_code(boost::system::errc::value_too_large, boost::system::system_category());
                m_ReceivedBytesLocal = 0;
                m_ReadBytes = 0;
                m_ParsedBytes = 0;
                return;
            }

            // obtain current message size
            const char* currentMessageBegin = &m_ReadBuffer[m_ParsedBytes];
            const auto& header = reinterpret_cast<const Header&>(*currentMessageBegin);
            m_MessageSize = header.m_Size;

            // check for complete packet
            if (m_MessageSize <= toParse - sizeof(Header))
            {
                const char* data = &m_ReadBuffer[m_ParsedBytes + sizeof(Header)];
                if (Settings::IsPersistent())
                {
                    m_Queue.UpdateAcknowledgedBytes(header.m_BytesReceived);
                }

                if (m_MessageSize)
                    InvokeCallback(data);

                m_ParsedBytes += (m_MessageSize + sizeof(Header));
                assert(m_ReadBytes >= m_ParsedBytes);
                if (m_ReadBytes < m_ParsedBytes)
                {
                    // broken packet
                    if (!e)
                        e = boost::system::error_code(boost::system::errc::protocol_error, boost::system::system_category());
                    m_ReceivedBytesLocal = 0;
                    m_ReadBytes = 0;
                    m_ParsedBytes = 0;
                    return;
                }
            }
            else
            {
                // this message has been read partially
                break;
            }
        }
    }

    void InvokeCallback(const char* data)
    {
        // need to save link to memory buffer
        class StreamWithMemory : public boost::iostreams::stream<net::BinaryReadStream>
        {
        public:
            StreamWithMemory(const char* data, std::size_t size, const boost::shared_ptr<char[]>& mem)
                : boost::iostreams::stream<net::BinaryReadStream>(data, size)
                , m_Memory(mem)
            {}
        private:
            const boost::shared_ptr<char[]> m_Memory;
        };

        const auto stream = boost::make_shared<StreamWithMemory>(data, m_MessageSize, m_ReadBuffer);

        // invoke callback
        Base::m_Callback(stream);
    }

protected:
    boost::asio::io_service::strand m_Strand;
    boost::shared_ptr<char[]> m_ReadBuffer;
    boost::uint32_t m_ReadBytes;
    boost::uint32_t m_ParsedBytes;
    std::size_t m_BufferSize;
    std::size_t m_MinBufferSize;
    boost::uint64_t m_ReceivedBytesLocal;
    boost::uint64_t m_BytesWritten;
    typename Traits::Queue m_Queue;

private:
    boost::asio::io_service& m_Service;
    const Settings m_Settings;

    std::atomic_bool m_IsReadBufferBusy;
    std::size_t m_MessageSize;
    mutable boost::mutex m_Mutex;
};

} // namespace details
} // namespace net
