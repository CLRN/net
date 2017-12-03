#include "net/sequence.hpp"
#include "net/details/stream.hpp"
#include "conversion/cast.hpp"
#include "net/exception.hpp"

#include <map>

#include <boost/make_shared.hpp>
#include <boost/noncopyable.hpp>
#include <boost/static_assert.hpp>
#include <boost/thread.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace net
{

namespace seq
{

#pragma pack(push, 1)

struct Header
{
    typedef boost::uint16_t Id;

    enum Value : char
    {
        END_SEQUENCE_MARKER   = 0,
        BLOCK_MARKER          = 1
    };

    Id m_Id;
    Value m_Marker;
};

#pragma pack(pop)

BOOST_STATIC_ASSERT(sizeof(Header) == 3);

} // namespace seq


SequencedConnection::SequencedConnection(const IConnection::Ptr& connection) 
    : m_Connection(connection)
    , m_Id()
{
    static std::atomic<seq::Header::Id> counter(0);
    m_Id = counter++;
}

SequencedConnection::~SequencedConnection()
{
    try
    {
        // write EOF marker(means that sequence is completed)
        seq::Header header = { m_Id, seq::Header::END_SEQUENCE_MARKER };
        const auto data = m_Connection->Prepare(sizeof(header));
        data->Write(&header, sizeof(header));
    }
    catch (const std::exception&)
    {
        if (!std::uncaught_exception())
            throw;
    }
}

details::IData::Ptr SequencedConnection::Prepare(std::size_t size)
{
    seq::Header header = { m_Id, seq::Header::BLOCK_MARKER };

    const auto data = m_Connection->Prepare(size + sizeof(header));
    data->Write(&header, sizeof(header));

    return data;
}

void SequencedConnection::Receive(const Callback& callback)
{
    m_Connection->Receive(callback);
}

void SequencedConnection::Close()
{
    m_Connection->Close();
}

void SequencedConnection::Flush()
{
    m_Connection->Flush();
}

std::string SequencedConnection::GetInfo() const
{
    return m_Connection->GetInfo();
}

ReadSequence::ReadSequence(Streams& streams) : m_Read()
{
    m_Streams.swap(streams);
    m_CurrentStream = m_Streams.begin();
}

ReadSequence::ReadSequence(const ReadSequence& other)
    : m_Streams(other.m_Streams)
    , m_Read(other.m_Read)
    , m_CurrentStream(m_Streams.begin())
{

}

std::streamsize ReadSequence::read(char_type* s, const std::streamsize n)
{
    std::streamsize total = 0;
    for (; m_CurrentStream != m_Streams.end(); ++m_CurrentStream)
    {
        const auto& stream = *m_CurrentStream;
        if (!stream->tellg()) // stream beginning
            stream->seekg(sizeof(seq::Header), std::ios::beg); // skip header

        const auto toRead = n - total;
        stream->read(s + total, toRead);
        const std::streamsize bytes = stream->gcount();
        total += bytes;
        m_Read += bytes;

        if (total == n)
            return n; // read full buffer

        // read completely, use next stream
        assert(bytes < toRead);
    }

    return total;
}

std::streamsize ReadSequence::write(const char_type* /*s*/, std::streamsize /*n*/)
{
    assert(!"not implemented");
    return 0;
}

std::streampos ReadSequence::seek(std::streamoff offset, std::ios::seekdir dir)
{
    if (dir == std::ios::cur)
    {
        assert(!offset);
        return m_Read;
    }

    m_CurrentStream = m_Streams.begin();
    if (dir == std::ios::end)
        offset = std::numeric_limits<std::streamoff>::max();

    for (const auto& stream : m_Streams)
        stream->clear(std::ios::goodbit);

    m_Read = offset;
    for (const auto& stream : m_Streams)
    {
        if (offset)
        {
            // there is offset remaining, obtain current stream size
            stream->seekg(0, std::ios::end);
            const std::streamoff size = static_cast<std::streamoff>(stream->tellg()) - sizeof(seq::Header);
            if (size > offset)
            {
                stream->seekg(offset + sizeof(seq::Header));
                offset = 0;
            }
            else
            {
                offset -= size;
            }
        }
        else
        {
            // there is no offset value remaining, reset current stream to the beginning
            stream->seekg(sizeof(seq::Header), std::ios::beg);
        }
    }

    if (dir == std::ios::end)
    {
        // calculate streams size
        const auto size = std::numeric_limits<std::streamoff>::max() - offset;
        m_Read = size;
        return size;
    }

    return offset;
}

namespace 
{

class StreamCollector : public ISequenceCollector, boost::noncopyable
{
public:
#if ((UINTPTR_MAX) == (UINT64_MAX))
    enum { MAX_STREAM_SIZE_IN_MEMORY = ULONG_MAX };
#else
    enum { MAX_STREAM_SIZE_IN_MEMORY = 1024 * 1024 * 10 }; // 10 mb
#endif

    struct StreamDesc
    {
        StreamDesc() : m_Size(), m_IsMemory(true) {}
        ReadSequence::Streams m_Streams;
        std::size_t m_Size;
        bool m_IsMemory;
    };

    typedef std::map<seq::Header::Id, StreamDesc> StreamMap;

    StreamCollector(const ICallback::Ptr& c) 
        : m_Callback(c) 
    {
    }

    ~StreamCollector()
    {

    }

    virtual void OnNewStream(const ReadSequence::StreamPtr& stream) override
    {
        if (!stream)
        {
            if (const auto cb = m_Callback.lock())
                cb->OnFullStreamCollected(stream);

            boost::unique_lock<boost::mutex> lock(m_Mutex);
            m_Streams.clear();
            return;
        }

        // calculate size
        stream->seekg(0, std::ios::end);
        const std::size_t size = static_cast<std::size_t>(stream->tellg());
        stream->seekg(0, std::ios::beg);

        // read sequence id and block marker
        seq::Header header;
        stream->read(reinterpret_cast<char*>(&header), sizeof(seq::Header));

        if (header.m_Marker == seq::Header::BLOCK_MARKER)
        {
            // collect stream
            boost::unique_lock<boost::mutex> lock(m_Mutex);
            auto& desc = m_Streams[header.m_Id];

            if (desc.m_IsMemory)
            {
                if (desc.m_Size > MAX_STREAM_SIZE_IN_MEMORY)
                {
                    desc.m_Streams.push_back(stream);

                    const auto file = CreateStream();
                    for (const auto& src : desc.m_Streams)
                        CopyStream(*src, *file);

                    desc.m_Streams.clear();
                    desc.m_Streams.push_back(file);
                    desc.m_IsMemory = false;
                }
                else
                {
                    desc.m_Streams.push_back(stream);
                }
            }
            else
            {
                assert(desc.m_Streams.size() == 1);
                auto& file = dynamic_cast<std::ostream&>(*desc.m_Streams.front());
                CopyStream(*stream, file);
            }

            desc.m_Size += size;
        }
        else
        if (header.m_Marker == seq::Header::END_SEQUENCE_MARKER)
        {
            // stream is over, invoke callback
            ReadSequence::Streams streams;
            bool isInFile = false;

            {
                boost::unique_lock<boost::mutex> lock(m_Mutex);
                const auto it = m_Streams.find(header.m_Id);
                if (it != m_Streams.end())
                {
                    isInFile = !it->second.m_IsMemory;
                    streams.swap(it->second.m_Streams);
                    m_Streams.erase(it);
                }
            }

            if (size > sizeof(seq::Header))
            {
                if (isInFile)
                    CopyStream(*stream, dynamic_cast<std::ostream&>(*streams.front()));
                else
                    streams.push_back(stream);
            }

            if (streams.empty())
                return;

            if (isInFile)
            {
                const auto size = streams.size();
                if (size != 1)
                    BOOST_THROW_EXCEPTION(Exception("Wrong sequence count, one stream expected: %s", size));
                streams.front()->seekg(0, std::ios::beg);
                *streams.front() >> std::noskipws;

                if (const auto cb = m_Callback.lock())
                    cb->OnFullStreamCollected(streams.front());
            }
            else
            {
                // make read sequence
                const auto sequence = boost::make_shared<ReadSequence::Stream>(streams);
                *sequence >> std::noskipws;
                if (const auto cb = m_Callback.lock())
                    cb->OnFullStreamCollected(sequence);
            }
        }
        else
        {
            BOOST_THROW_EXCEPTION(Exception("Wrong header marker: %s, id: %s", header.m_Marker, header.m_Id));
        }
    }

    boost::shared_ptr<std::iostream> CreateStream()
    {
        const auto uuid = boost::uuids::random_generator()();
        const auto path = boost::filesystem::path("/tmp") / conv::cast<std::string>(uuid);

        {
            boost::filesystem::ofstream test(path);
            if (!test.is_open())
                BOOST_THROW_EXCEPTION(Exception("Failed to open stream: %s", path.string()));
        }

        boost::shared_ptr<boost::filesystem::fstream> stream(
            new boost::filesystem::fstream(path, std::ios::binary | std::ios::in | std::ios::out),
            [path](boost::filesystem::fstream* s)
            {
                delete s;
                boost::filesystem::remove(path);
            }
        );

        if (!stream->is_open())
            BOOST_THROW_EXCEPTION(Exception("Failed to open stream: %s", path.string()));

        return stream;
    }

private:
    ICallback::Ptr m_Callback;
    StreamMap m_Streams;
    boost::mutex m_Mutex;
};


} // anonymous namespace 


ISequenceCollector::Ptr ISequenceCollector::Instance(const ICallback::Ptr& callback)
{
    return boost::make_shared<StreamCollector>(callback);
}

} // namespace net