#include "exception.hpp"
#include "net/settings.hpp"

#include <deque>

#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/function.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace net
{
namespace details
{

template
<
        class Allocator,
        class Settings = DefaultSettings
>
class PersistentQueue
{
public:
    struct MemHolder
    {
        typename Allocator::Memory m_Memory;
        boost::uint32_t m_Size;
    };

    typedef boost::shared_ptr<PersistentQueue> Ptr;
    typedef boost::function<void(const MemHolder&)> Callback;

    PersistentQueue(const Callback& cb)
        : m_Callback(cb)
        , m_ByteSize()
        , m_ReadOffset()
        , m_CurrentPacket()
        , m_CurrentSize()
    {

    }

    ~PersistentQueue()
    {
        Clear();
    }

    bool IsEmpty() const
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        return !m_ByteSize;
    }

    void Clear()
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        ClearData();
    }

    void Push(MemHolder&& holder)
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);
        m_ByteSize += holder.m_Size;

        if (m_FilePath.empty())
        {
            // store in the memory queue
            m_Queue.emplace_back(std::move(holder));

            // queue is not persistent yet
            if (m_Queue.size() < Settings::GetQueueMaxElemCount() && m_ByteSize < Settings::GetQueueMaxByteSize())
            {
                // store in memory queue
                if (m_Queue.size() == 1)
                {
                    // first packet, need to invoke write
                    auto copy = m_Queue.front();
                    lock.unlock();
                    m_Callback(copy);
                }
            }
            else
            {
                // limit reached, persist queue
                m_FilePath = boost::filesystem::path("net") / "queue";
                if (!boost::filesystem::exists(m_FilePath))
                    boost::filesystem::create_directories(m_FilePath);

                m_FilePath /= boost::uuids::to_string(boost::uuids::uuid());
                {
                    boost::filesystem::ofstream t(m_FilePath);
                }

                m_Stream.open(m_FilePath, std::ios::binary | std::ios::out | std::ios::in);
                m_Stream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                if (!m_Stream.is_open())
                    BOOST_THROW_EXCEPTION(Exception("Failed to open stream: %s", m_FilePath.string()));

                // save first element, because it's in pending write
                m_CurrentPacket = m_Queue.front();
                m_Queue.pop_front();

                // write all queue
                m_Stream.seekg(0, std::ios::end);
                for (const auto& elem : m_Queue)
                    Write(elem);

                m_Queue.clear();
            }
        }
        else
        {
            // persistent queue already exist, write to the end
            m_Stream.seekg(0, std::ios::end);
            Write(holder);
        }
    }

    void Pop()
    {
        boost::unique_lock<boost::mutex> lock(m_Mutex);

        // check persistent queue
        if (m_FilePath.empty())
        {
            // there is no persistent queue yet
            if (!m_Queue.empty())
            {
                m_ByteSize -= m_Queue.front().m_Size;
                m_Queue.pop_front();
            }

            if (!m_Queue.empty())
            {
                const auto next = m_Queue.front();
                lock.unlock();

                // write next packet
                m_Callback(next);
            }
        }
        else
        {
            // all data must be in the file buffer
            assert(m_Queue.empty());

            m_ByteSize -= m_CurrentPacket.m_Size;
            if (!m_ByteSize)
            {
                // we have just written last packet, clear persistent queue
                m_Stream.seekg(0, std::ios::end);
                assert(m_Stream.tellg() == m_ReadOffset);
                ClearData();
                return;
            }

            // extract last packet from persistent queue
            m_Stream.seekg(m_ReadOffset);

            m_Stream.read(reinterpret_cast<char*>(&m_CurrentPacket.m_Size), sizeof(m_CurrentPacket.m_Size));

            if (m_CurrentSize < m_CurrentPacket.m_Size)
            {
                // not enough memory, allocate new buffer
                m_CurrentPacket.m_Memory = Allocator::Allocate(m_CurrentPacket.m_Size);
                m_CurrentSize = m_CurrentPacket.m_Size;
            }

            m_Stream.read(m_CurrentPacket.m_Memory.get(), m_CurrentPacket.m_Size);

            // increment read offset
            m_ReadOffset += (m_CurrentPacket.m_Size + sizeof(m_CurrentPacket.m_Size));
            lock.unlock();

            // write packet
            m_Callback(m_CurrentPacket);
        }
    }

private:

    void ClearData()
    {
        m_ReadOffset = 0;
        m_ByteSize = 0;
        m_CurrentSize = 0;
        m_CurrentPacket.m_Memory.reset();
        if (!m_FilePath.empty())
        {
            m_Stream.close();
            boost::system::error_code e;
            boost::filesystem::remove(m_FilePath, e);
            m_FilePath.clear();
        }
    }

    void Write(const MemHolder& holder)
    {
        m_Stream.write(reinterpret_cast<const char*>(&holder.m_Size), sizeof(holder.m_Size));
        m_Stream.write(holder.m_Memory.get(), holder.m_Size);
    }

private:
    boost::filesystem::fstream m_Stream;
    boost::filesystem::path m_FilePath;
    const Callback m_Callback;
    unsigned m_ByteSize;
    std::streampos m_ReadOffset;

    mutable boost::mutex m_Mutex;
    std::deque<MemHolder> m_Queue;
    MemHolder m_CurrentPacket;
    boost::uint32_t m_CurrentSize;
};

} // namespace details
} // namespace net
