#pragma once

#include "net/details/channel.hpp"
#include "net/details/params.hpp"

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
    virtual void Read(const typename Base::Buffer& buffer) override
    {
        boost::system::error_code e;
        const auto bytes = Base::m_IoObject->read_some(buffer, e);
        Base::m_IoObject->get_io_service().post(
                boost::bind(&Base::ReadMessageCallback, Base::Shared(), e, bytes));
    }


    virtual void Write(const MemHolder& holder) override
    {
        boost::system::error_code e;
        const auto written = boost::asio::write(*Base::m_IoObject,
                                                boost::asio::buffer(holder.m_Memory.get(), holder.m_Size),
                                                boost::asio::transfer_all(),
                                                e);
        Base::WriteCallback(e, written, holder);
    }
};

} // namespace channels
} // namespace net
