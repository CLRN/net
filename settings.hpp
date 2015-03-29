#pragma once

namespace net
{

class DefaultSettings
{
public:
    static std::size_t GetQueueMaxElemCount()
    {
        return 10000;
    }
    static std::size_t GetQueueMaxByteSize()
    {
        return 1024 * 1024 * 100; // 100 Mb
    }
    static std::size_t GetMaxMessageSize()
    {
        return 1024 * 1024 * 50; // 50 Mb
    }
    static std::size_t GetBufferSize()
    {
        return 4096;
    }

};

} // namespace net
