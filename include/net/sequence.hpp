#pragma once

#include "connection.hpp"
#include "details/memory.hpp"

#include <atomic>
#include <list>

#include <boost/iostreams/stream.hpp>
#include <boost/cstdint.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/weak_ptr.hpp>

namespace net
{

// стрим-like интерфейс для группировки пакетов данных
// в режиме записи:
// создает идентификатор последовательности
// к каждый отправляемый через него пакет маркирует идентификатором
// отправляет пакет означающий окончание последовательности
class SequencedConnection : public IConnection
{
public:
    SequencedConnection(const IConnection::Ptr& connection);
    ~SequencedConnection();

    virtual details::IData::Ptr Prepare(std::size_t size) override;
    virtual void Receive(const Callback& callback) override;
    virtual void Close() override;
    virtual void Flush() override;
    virtual std::string GetInfo() const override;

private:
private:
    IConnection::Ptr m_Connection;
    boost::uint16_t m_Id;
    boost::mutex m_Mutex;
};

// в режиме чтения:
// получает указатель на входящий стрим с данными, получает идентификатор последовательности
// группирует входящие стримы по идентификатору
// получает пакет завершения последовательности, вызывает заданный коллбэк с input стримом содержащим все данные последовательности
class ReadSequence
{
public:
    typedef char char_type;
    typedef boost::iostreams::seekable_device_tag category;
    typedef boost::iostreams::stream<ReadSequence> Stream;
    typedef boost::shared_ptr<std::istream> StreamPtr;
    typedef std::list<StreamPtr> Streams;

    ReadSequence(Streams& streams);
    ReadSequence(const ReadSequence& other);

    std::streamsize read(char_type* s, std::streamsize n);
    std::streamsize write(const char_type* s, std::streamsize n);
    std::streampos seek(std::streamoff offset, std::ios::seekdir dir);

private:
    Streams m_Streams;
    std::istream::streampos m_Read;
    Streams::const_iterator m_CurrentStream;
};

class ISequenceCollector
{
public:

    class ICallback
    {
    public:
        typedef boost::weak_ptr<ICallback> Ptr;
        virtual void OnFullStreamCollected(const ReadSequence::StreamPtr& stream) = 0;
    };

    typedef boost::shared_ptr<ISequenceCollector> Ptr;

    virtual ~ISequenceCollector() {}
    virtual void OnNewStream(const ReadSequence::StreamPtr& stream) = 0;

    static Ptr Instance(const ICallback::Ptr& callback);
};

} // namespace net

#include "details/sequence_impl.hpp"

