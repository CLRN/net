#pragma once

namespace net
{

class DefaultSettings
{
public:
    std::size_t GetQueueMaxElemCount() const
    {
        return 10000;
    }
    std::size_t GetQueueMaxByteSize() const
    {
        return 1024 * 1024 * 100; // 100 Mb
    }
    std::size_t GetMaxMessageSize() const
    {
        return 1024 * 1024 * 50; // 50 Mb
    }
    std::size_t GetBufferSize() const
    {
        return 4096;
    }

};

} // namespace net
