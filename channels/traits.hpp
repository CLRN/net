#pragma once

namespace net
{
namespace details
{

template<typename H,
         typename Q,
         typename S,
         typename T>
class ChannelTraits
{
public:
    typedef H Handle;
    typedef Q Queue;
    typedef S Settings;
    typedef T Transport;
};

} // namespace details
} // namespace net