#pragma once

#include "net/details/channel.hpp"
#include "net/details/memory.hpp"
#include "net/details/params.hpp"
#include "net/details/settings.hpp"
#include "net/exception.hpp"

namespace net
{

namespace details
{

template
<
    typename Settings = DefaultSettings
>
class FakeQueue
{
    class LocalData : public net::details::IData
    {
    public:
        LocalData(const net::details::MemoryPool::WriteCallback& cb) 
            : m_Callback(cb)
        {}

        virtual void Write(const void* data, std::size_t size) override
        {
            m_Callback(details::MemoryPool::Buffers({ boost::asio::buffer(data, size) }));
        }

        virtual const std::vector<boost::asio::mutable_buffer>& GetBuffers() override
        {
            static const std::vector<boost::asio::mutable_buffer> res;
            return res;
        }

    private:
        const net::details::MemoryPool::WriteCallback m_Callback;
    };

public:
    template<typename ... Args>
    FakeQueue(const boost::function<void(const std::vector<boost::asio::const_buffer>&)>& cb, const Args&... args)
        : m_Callback(cb)
    {

    }

    details::IData::Ptr Prepare(std::size_t)
    {
        return boost::make_shared<LocalData>(m_Callback);
    }

    void OnWriteCompleted(uint64_t bytes)
    {
    }

    void OnWriteFailed(uint64_t bytes)
    {
    }

    void UpdateAcknowledgedBytes(uint64_t bytes)
    {
    }

    void Reset()
    {
    }

    bool IsEmpty() const
    {
        return true;
    }

    void Clear()
    {
    }

    void OnFailure()
    {

    }
private:
    const boost::function<void(const std::vector<boost::asio::const_buffer>&)> m_Callback;
};

} // namespace details

namespace channels
{

//! Channel type
template<typename Traits>
class SyncQueue : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    SyncQueue(const Args&... args)
        : Base(args...)
    {
    }

    //! Begin asynchronous read to buffer
    virtual void DoRead(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->Read(
            boost::asio::buffer_cast<void*>(buffer),
            boost::asio::buffer_size(buffer),
            Base::m_Strand.wrap(boost::bind(&Base::ReadMessageCallback, Base::Shared(), _1, _2, Base::m_IoObject))
        );   
    }

    virtual void DoWrite(const std::vector<boost::asio::const_buffer>& buffers) override
    {
        std::size_t size = 0;

        for (const auto& buffer : buffers)
        {
            const auto current = boost::asio::buffer_size(buffer);
            Base::m_IoObject->Write(boost::asio::buffer_cast<const char*>(buffer), current);
            size += current;
        }

        Base::WriteCallback(boost::system::error_code(), size, Base::m_IoObject);
    }

private:
};

} // namespace channels
} // namespace net
