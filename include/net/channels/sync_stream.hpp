#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

#include <boost/asio.hpp>

namespace net
{
namespace channels
{

//! Channel type
template<typename Traits>
class SyncStream : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    SyncStream(const Args&... args)
        : Base(args...)
    {
    }

    //! Begin asynchronous read to buffer
    virtual void DoRead(const typename Base::Buffer& buffer) override
    {
        Base::m_IoObject->async_read_some(
            buffer,
            Base::m_Strand.wrap(boost::bind(&Base::ReadMessageCallback, Base::Shared(), _1, _2, Base::m_IoObject))
        );
    }


    virtual void DoWrite(const std::vector<boost::asio::const_buffer>& buffers) override
    {
        boost::system::error_code e;
        const auto written = boost::asio::write(*Base::m_IoObject,
                                                buffers,
                                                e);
        Base::WriteCallback(e, written, Base::m_IoObject);
    }
};

} // namespace channels
} // namespace net
