#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

namespace net
{
namespace channels
{

//! Channel type
template<typename Traits>
class AsyncStream : public net::details::Channel<Traits>
{
public:
    typedef details::Channel<Traits> Base;

    template<typename ... Args>
    AsyncStream(const Args&... args)
        : Base(args...)
        , m_IsReading()
    {
    }

    //! Begin asynchronous read to buffer
    virtual void DoRead(const typename Base::Buffer& buffer) override
    {
        assert(!m_IsReading);
        m_IsReading = true;

        const auto object = Base::m_IoObject;
        const auto instance = Base::Shared();

        Base::m_IoObject->async_read_some(
            buffer,
            Base::m_Strand.wrap([instance, object, this](boost::system::error_code e, std::size_t bytes)
        {
            m_IsReading = false;
            Base::ReadMessageCallback(e, bytes, object);
        }));
    }

    virtual void DoWrite(const std::vector<boost::asio::const_buffer>& buffers) override
    {
        boost::asio::async_write(
                *Base::m_IoObject,
                buffers,
                Base::m_Strand.wrap(boost::bind(&AsyncStream::WriteCallback, Base::Shared(), _1, _2, Base::m_IoObject))
        );
    }

private:
    bool m_IsReading;
};

} // namespace channels
} // namespace net
