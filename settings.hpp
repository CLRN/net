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
};

} // namespace net
