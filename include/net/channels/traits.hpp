#pragma once

namespace net
{
namespace details
{

template<typename HandleType,
         typename QueueType,
         typename SettingsType,
         typename TransportType,
         typename HeaderType>
class ChannelTraits
{
public:
    typedef HandleType Handle;
    typedef QueueType Queue;
    typedef SettingsType Settings;
    typedef TransportType Transport;
    typedef HeaderType Header;
};

} // namespace details
} // namespace net