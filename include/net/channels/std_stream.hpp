#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Traits>
class StdStream : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    StdStream(typename Traits::Handle& in, const Args&... args)
        : Base(args...)
        , m_In(in)
    {
    }

    virtual void OnBytesRead(const std::size_t bytes) override
    {
        if (bytes == sizeof(boost::uint32_t) &&
            !reinterpret_cast<const boost::uint32_t&>(this->m_ReadBuffer[this->m_ParsedBytes]))
        {
            // special case for connection close, child process need to unblock
            // input stream to be able to disconnect gracefully
            boost::uint32_t size = 0;
            DoWrite({ boost::asio::buffer(&size, sizeof(size)) });
        }
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
        m_In->write_some(buffers);
        this->WriteCallback(boost::system::error_code(), boost::asio::buffer_size(buffers), Base::m_IoObject);
    }

private:
    typename Traits::Handle m_In;
};

} // namespace channels
} // namespace net
