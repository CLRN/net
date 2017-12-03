#pragma once

#include "net/exception.hpp"
#include "net/details/memory.hpp"
#include "net/details/params.hpp"
#include "net/details/settings.hpp"

#include <deque>
#include <atomic>

#include <boost/thread/mutex.hpp>

namespace net
{
namespace details
{

template
<
    typename Settings = DefaultSettings
>
class PersistentQueue
{
public:
    template<typename Callback, typename ... Args>
    PersistentQueue(const Callback& callback, const Args&... args)
        : m_Allocated()
        , m_Acknowledged()
        , m_Written()
        , m_MemoryPool(boost::make_shared<MemoryPool>(callback))
    {

    }

    ~PersistentQueue()
    {
        Clear();
    }

    bool IsEmpty() const
    {
        return m_MemoryPool->IsEmpty();
    } 

    void Clear()
    {
        m_MemoryPool->Clear();
        m_Acknowledged = 0;
        m_Allocated = 0;
    }

    details::IData::Ptr Prepare(uint64_t size)
    {
        m_Allocated += size;
        return m_MemoryPool->Allocate(size);
    }

    void OnWriteCompleted(uint64_t bytes)
    {
        m_Written += bytes;
        m_MemoryPool->OnWriteCompleted(bytes);
    }

    void OnWriteFailed(uint64_t bytes)
    {
        m_Written += bytes;
        m_MemoryPool->OnWriteFailed(bytes);
    }

    void UpdateAcknowledgedBytes(uint64_t bytes)
    {
        m_Acknowledged = bytes;
        m_MemoryPool->UpdateAcknowledgedBytes(bytes);
    }

    void Reset()
    {
        m_MemoryPool->Reset();
    }

    void OnFailure()
    {
        m_MemoryPool->OnFailure();
    }

private:
    std::atomic<uint64_t> m_Allocated;
    std::atomic<uint64_t> m_Acknowledged;
    std::atomic<uint64_t> m_Written;

    const details::MemoryPool::Ptr m_MemoryPool;

};

} // namespace details
} // namespace net
