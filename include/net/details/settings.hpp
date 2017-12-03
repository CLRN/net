#pragma once

namespace net
{

class DefaultSettings
{
public:
    static std::size_t GetQueueMaxElemCount()
    {
#if ((UINTPTR_MAX) == (UINT64_MAX))
        return UINT64_MAX;
#else
        return 10000;
#endif
        
    }
    static std::size_t GetQueueMaxByteSize()
    {
#if ((UINTPTR_MAX) == (UINT64_MAX))
        return UINT64_MAX;
#else
        return 1024 * 1024 * 100; // 100 Mb
#endif
    }
    static std::size_t GetBufferSize()
    {
#if ((UINTPTR_MAX) == (UINT64_MAX))
        return 1024 * 1024;
#else
        return 4096;
#endif
    }

    static bool IsPersistent()
    {
        return false;
    }

};

class PersistentSettings
{
public:
    static std::size_t GetQueueMaxElemCount()
    {
        return DefaultSettings::GetQueueMaxElemCount();
    }
    static std::size_t GetQueueMaxByteSize()
    {
        return DefaultSettings::GetQueueMaxByteSize();
    }
    static std::size_t GetBufferSize()
    {
        return DefaultSettings::GetBufferSize();
    }

    static bool IsPersistent()
    {
        return true;
    }

};


} // namespace net
