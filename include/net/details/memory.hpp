#pragma once

#include <deque>
#include <list>
#include <vector>
#include <atomic>

#include "log/log.h"

#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/function.hpp>
#include <boost/unordered/unordered_map.hpp>
#include <boost/iostreams/categories.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>


#ifdef min
#undef min
#endif

namespace net
{

SET_LOGGING_MODULE("Network");

namespace details
{


//! Data workflow is following:
//! 1. Data is allocated in one or more chunks increasing m_Allocated and assigned to IData
//! so the caller can write to the allocated memory using IData interface
//! 2. Data is marked ready by the caller through IData interface increasing m_Ready counter.
//! Then the memory pool is informed and data write is performed
//! 3. Data is written to the socket or other remote endpoint increasing m_Extracted counter
//! 4. When the write operation is confirmed and we receive bytes actually written m_Released counter is increased
//! 5. When remote side acknowledges the bytes received memory pool can free data in buffers increasing m_Freed counter


class IChunk
{
public:
    typedef boost::shared_ptr<IChunk> Ptr;

    virtual ~IChunk() {}

    virtual bool IsFull() const = 0;
    virtual bool IsEmpty() const = 0;
    virtual bool IsInMemory() const = 0;
    virtual uint64_t GetSize() const = 0;

    virtual boost::asio::const_buffer GetBuffer(bool& moreData) = 0;
    virtual boost::asio::mutable_buffer Allocate(uint64_t& size) = 0;
    virtual void Ready(const boost::asio::mutable_buffer& buffer) = 0;
    virtual void Free(uint64_t& bytes) = 0;
    virtual void Release(uint64_t& bytes) = 0;
    virtual void UpdateExtracted(uint64_t& bytes) = 0;
};

class IData
{
public:
    typedef boost::shared_ptr<IData> Ptr;

    virtual ~IData() {}

    virtual void Write(const void* data, std::size_t size) = 0;
    virtual const std::vector<boost::asio::mutable_buffer>& GetBuffers() = 0;
};

class StreamWrapper
{
public:
    typedef char char_type;
    typedef boost::iostreams::sink_tag category;

    StreamWrapper(const IData::Ptr& data) : m_Data(data) {}

    std::streamsize write(const char_type* s, std::streamsize n)
    {
        m_Data->Write(s, static_cast<std::size_t>(n));
        return n;
    }

private:
    IData::Ptr m_Data;
};


class MemoryPool : public boost::enable_shared_from_this<MemoryPool>
{
public:
    typedef boost::shared_ptr<MemoryPool> Ptr;
    typedef std::vector<boost::asio::const_buffer> Buffers;
    typedef boost::function<void(const Buffers& buffers)> WriteCallback;
private:

    class MemoryChunk : public IChunk, boost::noncopyable
    {
        typedef std::list<boost::asio::mutable_buffer> Buffers;
    public:
        MemoryChunk(uint64_t size, bool allocate = true)
            : m_Data(reinterpret_cast<char*>(allocate ? malloc(static_cast<std::size_t>(size)) : nullptr))
            , m_Size(size)
            , m_Allocated()
            , m_Ready()
            , m_Extracted()
            , m_Released()
            , m_Freed()
            , m_IsBuffersAllocated()
        {

        }

        ~MemoryChunk()
        {
            free(m_Data);
        }

        virtual void Open(const uint64_t newSize)
        {
        }

        virtual void Close()
        {

        }

        virtual boost::asio::mutable_buffer Allocate(uint64_t& size) override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            if (!m_Data)
                Open(0);

            size = std::min(size, m_Size - m_Allocated);
            const auto start = m_Allocated;

            const auto result = boost::asio::buffer(m_Data + start, static_cast<std::size_t>(size));
            m_AllocatedMap.emplace(std::make_pair(m_Data + start, m_AllocatedBuffers.insert(m_AllocatedBuffers.end(), result)));
            m_Allocated += size;

            return result;
        }

        virtual boost::asio::const_buffer GetBuffer(bool& moreData) override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            if (!m_Data)
                Open(0);

            const char* begin = m_Data + m_Extracted;

            const auto* end = m_Data + m_Allocated;
            if (!m_AllocatedBuffers.empty())
                end = boost::asio::buffer_cast<const char*>(m_AllocatedBuffers.front());

            const auto size = end - begin;
            m_Extracted += size;

            // more data exists only if just have prepared all the remaining data
            moreData = m_Extracted == m_Allocated;
            m_IsBuffersAllocated = !!size;

            return boost::asio::buffer(begin, size);
        }

        virtual void Ready(const boost::asio::mutable_buffer& buffer) override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);

            const auto* current = boost::asio::buffer_cast<const char*>(buffer);

            // remove buffer from allocated
            const auto it = m_AllocatedMap.find(current);
            assert(it != m_AllocatedMap.end());
            m_AllocatedBuffers.erase(it->second);
            m_AllocatedMap.erase(it);

            if (!m_IsBuffersAllocated && m_AllocatedBuffers.empty() && m_Size == m_Allocated)
                Close(); // do not keep all the memory here
        }

        virtual void Free(uint64_t& bytes) override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);

            bytes = std::min(bytes, m_Allocated);
            m_Freed = bytes;
            m_IsBuffersAllocated = false;

            if (!m_IsBuffersAllocated && m_Size == m_Freed && m_AllocatedBuffers.empty())
            {
                // reset chunk
                m_Allocated = 0;
                m_Ready = 0;
                m_Extracted = 0;
                m_Released = 0;
                m_Freed = 0;
                Close();
            }
        }

        virtual void Release(uint64_t& bytes) override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            bytes = std::min(bytes, m_Allocated - m_Released);
            m_Released += bytes;

            if (!m_IsBuffersAllocated && m_Released == m_Size && m_AllocatedBuffers.empty())
                Close();
        }

        virtual void UpdateExtracted(uint64_t& bytes) override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            bytes = std::min(bytes, m_Extracted);
            m_Extracted = bytes;
        }

        virtual bool IsFull() const override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            return m_Size == m_Allocated;
        }

        virtual bool IsEmpty() const override
        {
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            return !m_Allocated;
        }

        virtual bool IsInMemory() const override
        {
            return true;
        }

        virtual uint64_t GetSize() const override
        {
            return m_Size;
        }

    protected:
        char* m_Data;
        const uint64_t m_Size;
        uint64_t m_Allocated;
        uint64_t m_Ready;
        uint64_t m_Extracted;
        uint64_t m_Released;
        uint64_t m_Freed;
        bool m_IsBuffersAllocated;

        Buffers m_AllocatedBuffers;
        boost::unordered_map<const char*, Buffers::const_iterator> m_AllocatedMap;

        mutable boost::mutex m_Mutex;
    };

    class FileSinkChunk : public MemoryChunk
    {
    public:
        FileSinkChunk(uint64_t size, std::atomic<std::size_t>& counter)
            : MemoryChunk(size, false)
            , m_Counter(counter)
            , m_Path(boost::filesystem::path("/tmp") / (boost::lexical_cast<std::string>(boost::uuids::random_generator{}()) + ".netmem"))
        {
            Open(size);
            ++m_Counter;
        }

        ~FileSinkChunk()
        {
            --m_Counter;
            boost::unique_lock<boost::mutex> lock(m_Mutex);

            Close();
            boost::filesystem::remove(m_Path);
        }

        virtual void Open(const uint64_t newSize = 0) override
        {
            boost::iostreams::mapped_file_params params;
            params.path = m_Path.string();
            params.offset = 0;
            params.length = static_cast<std::size_t>(m_Size);
            params.new_file_size = newSize;

            m_Sink.open(params);
            m_Data = m_Sink.data();
        }

        void Close()
        {
            m_Sink.close();
            m_Data = nullptr;
        }

        virtual bool IsInMemory() const override
        {
            return false;
        }

    private:
        const boost::filesystem::path m_Path;
        boost::iostreams::mapped_file_sink m_Sink;
        std::atomic<std::size_t>& m_Counter;
    };

    class DataHolder : public IData
    {
    public:
        DataHolder(std::vector<boost::asio::mutable_buffer>&& buffers, 
                   const std::vector<IChunk::Ptr>& chunks,
                   const MemoryPool::Ptr& parent)
            : m_Parent(parent)
            , m_Chunks(chunks)
            , m_Buffers(std::move(buffers))
            , m_Index()
            , m_Offset()
            , m_Written()
        {}

        ~DataHolder()
        {
            assert(m_Written == boost::asio::buffer_size(m_Buffers));

            for (std::size_t i = 0; i < m_Chunks.size(); ++i)
                m_Chunks[i]->Ready(m_Buffers[i]);

            if (const auto parent = m_Parent.lock())
                parent->OnReadyToSend();
        }

        virtual void Write(const void* data, std::size_t size) override
        {
            assert(boost::asio::buffer_size(m_Buffers) >= size);
            m_Written += size;

            const char* current = reinterpret_cast<const char*>(data);
            while (size)
            {
                auto currentSize = boost::asio::buffer_size(m_Buffers[m_Index]);
                if (m_Offset == currentSize)
                {
                    ++m_Index;
                    m_Offset = 0;

                    assert(m_Buffers.size() > m_Index);
                    currentSize = boost::asio::buffer_size(m_Buffers[m_Index]);
                }
                
                const auto toCopy = std::min(size, currentSize - m_Offset);
                memcpy(boost::asio::buffer_cast<char*>(m_Buffers[m_Index]) + m_Offset, current, toCopy);

                size -= toCopy;
                current += toCopy;
                m_Offset += toCopy;
            }
        }

        virtual const std::vector<boost::asio::mutable_buffer>& GetBuffers() override
        {
            return m_Buffers;
        }

    private:
        const boost::weak_ptr<MemoryPool> m_Parent;
        const std::vector<IChunk::Ptr> m_Chunks;
        std::vector<boost::asio::mutable_buffer> m_Buffers;
        std::size_t m_Index;
        std::size_t m_Offset;
        uint64_t m_Written;
    };

public:

    enum { MAX_CHUNK_SIZE_IN_MEMORY = 1024 * 1024 * 5 };
    enum { MAX_CHUNKS_IN_MEMORY = 10 };

    MemoryPool(const WriteCallback& cb, 
               uint64_t chunkSize = MAX_CHUNK_SIZE_IN_MEMORY,
               uint64_t maxMemoryChunks = MAX_CHUNKS_IN_MEMORY)
        : m_WriteCallback(cb)
        , m_ChunkSize(chunkSize)
        , m_MaxInMemoryChunks(maxMemoryChunks)
        , m_ChunksInFiles()
        , m_BytesAllocated()
        , m_BytesAcknowledged()
        , m_BytesWritten()
        , m_BytesReleased()
        , m_ChunksFreed()
        , m_IsSendInProgress()
        , m_IsPaused()
    {

    }

    IData::Ptr Allocate(uint64_t size)
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);

        m_BytesAllocated += size;

        std::vector<boost::asio::mutable_buffer> buffers;
        std::vector<IChunk::Ptr> chunks;

        buffers.reserve(m_Chunks.size());
        chunks.reserve(m_Chunks.size());

        while (size)  
        {
            if (m_Chunks.empty() || m_Chunks.back()->IsFull())
            {
                IChunk::Ptr chunk;
                if (!m_FreeChunks.empty())
                {
                    chunk = m_FreeChunks.front();
                    m_FreeChunks.pop_front();
                }
                else
                {
                    if (m_ChunksInFiles || m_Chunks.size() >= m_MaxInMemoryChunks)
                        chunk = boost::make_shared<FileSinkChunk>(m_ChunkSize, m_ChunksInFiles);
                    else
                        chunk = boost::make_shared<MemoryChunk>(m_ChunkSize);
                }

                m_Chunks.emplace_back(chunk);
            }

            uint64_t allocated = size;
            buffers.emplace_back(m_Chunks.back()->Allocate(allocated));
            chunks.emplace_back(m_Chunks.back());
            size -= allocated;
        }

        return boost::make_shared<DataHolder>(std::move(buffers), chunks, shared_from_this());
    }

    void Clear()
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        m_Chunks.clear();
        m_FreeChunks.clear();
        m_BytesAllocated = 0;
    }

    bool IsEmpty() const
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        return !m_IsSendInProgress && (m_BytesWritten == m_BytesReleased || m_IsPaused);
    }

    void OnWriteCompleted(uint64_t bytes)
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        LOG_TRACE("Write completed, bytes: %s, total written: %s, acknowledged: %s", bytes, m_BytesWritten, m_BytesAcknowledged);
        m_IsSendInProgress = false;       
        ReleaseBytes(bytes);
        OnReadyToSend();
    }

    void OnFailure()
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        LOG_DEBUG("Pausing memory pool, total written: %s, acknowledged: %s", m_BytesWritten, m_BytesAcknowledged);
        m_IsPaused = true;
    }

    void Reset()
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        LOG_TRACE("Updating written bytes: %s --> %s setting value from acknowledged, chunks freed: %s", m_BytesWritten, m_BytesAcknowledged, m_ChunksFreed);
        m_BytesWritten = m_BytesAcknowledged;
        m_BytesReleased = m_BytesWritten;
        m_IsSendInProgress = false;
        m_IsPaused = false;

        UpdatedExtractedBytes();
        OnReadyToSend();
    }

    void UpdateAcknowledgedBytes(const uint64_t bytes)
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        if (m_BytesAcknowledged >= bytes)
            return;

        if (bytes > m_BytesAllocated)
        {
            LOG_ERROR("Attempt to set ack bytes %s greater then allocated: %s", bytes, m_BytesAllocated);
            return;
        }

        const auto diff = bytes - m_BytesAcknowledged;
        LOG_TRACE("Updating acknowledged bytes: %s --> %s, bytes to free: %s", m_BytesAcknowledged, bytes, diff);
        m_BytesAcknowledged = bytes;
    }

    void OnWriteFailed(uint64_t bytes)
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);
        LOG_TRACE("Write failed, bytes: %s, total written: %s, acknowledged: %s", bytes, m_BytesWritten, m_BytesAcknowledged);
        m_IsSendInProgress = false;
        m_IsPaused = true;
    }

private:

    void ReleaseBytes(uint64_t bytes)
    {
        m_BytesReleased += bytes;

        auto chunks = m_Chunks;
        while (bytes && !chunks.empty())
        {
            auto temp = bytes;
            chunks.front()->Release(temp);
            bytes -= temp;
            chunks.pop_front();
        }
    }

    void UpdatedExtractedBytes()
    {
        const auto bytesFreed = m_ChunksFreed * m_ChunkSize;
        assert(m_BytesWritten >= bytesFreed);

        auto bytes = m_BytesWritten - bytesFreed;

        auto chunks = m_Chunks;
        while (!chunks.empty())
        {
            auto temp = bytes;
            chunks.front()->UpdateExtracted(temp);
            bytes -= temp;
            chunks.pop_front();
        }
    }

    void Free()
    {
        auto toFree = m_BytesAcknowledged - m_ChunksFreed * m_ChunkSize;
        if (!toFree)
            return;

        LOG_TRACE("Freeing %s bytes, ack: %s, skipping %s chunks and %s bytes", toFree, m_BytesAcknowledged, m_ChunksFreed, m_ChunksFreed * m_ChunkSize);

        auto chunks = m_Chunks;

        while (toFree && !chunks.empty())
        {
            auto freed = toFree;
            chunks.front()->Free(freed);
            toFree -= freed;
            chunks.pop_front();
        }

        while (!m_Chunks.empty() && m_Chunks.front()->IsEmpty())
            FreeNextChunk();
    }

    void OnReadyToSend()
    {
        boost::unique_lock<boost::recursive_mutex> lock(m_Mutex);

        if (m_IsSendInProgress || m_IsPaused)
            return;

        Free();

        Buffers buffers;
        uint64_t bytesInMemory = 0;
        for (const auto& chunk : m_Chunks)
        {
            bool moreData = false;

            const auto buffer = chunk->GetBuffer(moreData);
            const auto size = uint64_t(boost::asio::buffer_size(buffer));

            bytesInMemory += size;

            if (size)
                buffers.emplace_back(buffer);

            if (!moreData || bytesInMemory * 2 > m_ChunkSize * m_MaxInMemoryChunks)
                break;
        }

        m_IsSendInProgress = !buffers.empty();
        LOG_TRACE("%s %s bytes, total written: %s, acknowledged: %s", (m_IsSendInProgress ? "Writing" : "Skipping"), bytesInMemory, m_BytesWritten, m_BytesAcknowledged);

        m_BytesWritten += bytesInMemory;

        if (!buffers.empty())
            m_WriteCallback(buffers);
    }

    void FreeNextChunk()
    {
        ++m_ChunksFreed;
        const auto chunk = m_Chunks.front();

        if (m_ChunksInFiles && chunk->IsInMemory())
        {
            LOG_TRACE("Releasing memory chunk");
            m_Chunks.pop_front();
        }
        else
        {
            if (m_FreeChunks.size() < m_MaxInMemoryChunks)
            {
                m_FreeChunks.emplace_back(chunk);
                LOG_TRACE("Saving empty chunk in free list");
            }

            m_Chunks.pop_front();
        }
    }

private:
    const uint64_t m_ChunkSize;
    const uint64_t m_MaxInMemoryChunks;
    const WriteCallback m_WriteCallback;
    std::deque<IChunk::Ptr> m_Chunks;
    std::deque<IChunk::Ptr> m_FreeChunks;

    mutable boost::recursive_mutex m_Mutex;
    bool m_IsSendInProgress;
    bool m_IsPaused;
    std::atomic<std::size_t> m_ChunksInFiles;

    uint64_t m_BytesAllocated;
    uint64_t m_BytesWritten;
    uint64_t m_BytesReleased;
    uint64_t m_BytesAcknowledged;
    uint64_t m_ChunksFreed;
};

} // namespace details


} // namespace net

