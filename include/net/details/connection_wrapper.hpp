#pragma once

#include "net/connection.hpp"
#include "net/details/memory.hpp"

namespace net
{
namespace details
{

class ConnectionWrapper : public net::IConnection, public boost::enable_shared_from_this<ConnectionWrapper>
{
public:

    struct IDataHandler
    {
        typedef boost::shared_ptr<IDataHandler> Ptr;

        virtual ~IDataHandler() = default;
        virtual std::size_t GetSize(std::size_t size) = 0;
        virtual void Write(const void* data, std::size_t size) = 0;
        virtual void Read(std::istream_iterator<char>&& in, std::back_insert_iterator<std::vector<char>>&& out) = 0;
        virtual void Flush(IData& underlying) = 0;
    };

private:
    class DataWrapper : public net::details::IData
    {
    public:
        DataWrapper(const boost::shared_ptr<ConnectionWrapper>& connection,
                    const IData::Ptr& underlying)
            : m_Parent(connection)
            , m_Underlying(underlying)
        {}

        ~DataWrapper()
        {
            m_Parent->m_Handler->Flush(*m_Underlying);
        }

        virtual void Write(const void* data, std::size_t size) override
        {
            m_Parent->m_Handler->Write(data, size);
        }

        virtual const std::vector<boost::asio::mutable_buffer>& GetBuffers()
        {
            return m_Underlying->GetBuffers();
        }

    private:
        const boost::shared_ptr<ConnectionWrapper> m_Parent;
        const IData::Ptr m_Underlying;
    };

public:
    ConnectionWrapper(const net::IConnection::Ptr& underlying, const IDataHandler::Ptr& handler)
        : m_Underlying(underlying)
        , m_Handler(handler)
    {}

    virtual void Receive(const Callback& callback) override
    {
        const auto handler = m_Handler;
        m_Underlying->Receive([callback, handler](const StreamPtr& stream)
                              {
                                  if (!stream)
                                  {
                                      callback(stream);
                                      return;
                                  }

                                  const auto buffer = boost::make_shared<std::vector<char>>();
                                  buffer->reserve(net::StreamSize(*stream));

                                  handler->Read(std::istream_iterator<char>(*stream), std::back_inserter(*buffer));

                                  typedef boost::iostreams::stream<net::BinaryReadStream> Stream;

                                  boost::shared_ptr<Stream> result(
                                          new Stream(buffer->data(), buffer->size()),
                                          [buffer](Stream* s)
                                          {
                                              delete s;
                                          }
                                  );

                                    callback(result);
                              });
    }

    virtual void Close() override
    {
        m_Underlying->Close();
    }

    virtual net::details::IData::Ptr Prepare(std::size_t size) override
    {
        const auto realSize = m_Handler->GetSize(size);
        const auto data = m_Underlying->Prepare(realSize);

        return boost::make_shared<DataWrapper>(shared_from_this(), data);
    }

    virtual void Flush() override
    {
        m_Underlying->Flush();
    }
    virtual std::string GetInfo() const override
    {
        return m_Underlying->GetInfo();
    }

private:
    const net::IConnection::Ptr m_Underlying;
    const IDataHandler::Ptr m_Handler;
};

} // namespace details
} // namespace net
