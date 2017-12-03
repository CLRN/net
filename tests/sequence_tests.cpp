#include "net/sequence.hpp"

#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include <boost/assign/list_of.hpp>
#include <boost/make_shared.hpp>
#include <boost/random.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/bind.hpp>

struct TestConnection : public net::IConnection
{
    class LocalData : public net::details::IData
    {
    public:
        LocalData(TestConnection& connection) : m_Parent(connection) {}
        ~LocalData()
        {
            m_Parent.m_Data.emplace_back(std::move(m_Buffer));
        }

        virtual void Write(const void* data, std::size_t size) override
        {
            std::copy(reinterpret_cast<const char*>(data), reinterpret_cast<const char*>(data)+size, std::back_inserter(m_Buffer));
        }

        virtual const std::vector<boost::asio::mutable_buffer>& GetBuffers()
        {
            static const std::vector<boost::asio::mutable_buffer> empty;
            return empty;
        }

    private:
        std::string m_Buffer;
        TestConnection& m_Parent;
    };

public:
    virtual void Receive(const Callback& callback) override
    {
        throw std::runtime_error("The method or operation is not implemented.");
    }

    virtual void Close() override
    {
        throw std::runtime_error("The method or operation is not implemented.");
    }

    virtual net::details::IData::Ptr Prepare(std::size_t size) override
    {
        return boost::make_shared<LocalData>(*this);
    }

    virtual void Flush() override
    {
        throw std::logic_error("The method or operation is not implemented.");
    }
    virtual std::string GetInfo() const override
    {
        return "";
    }

    std::vector<std::string> m_Data;
};

struct CollectorCallback : public net::ISequenceCollector::ICallback
{
    virtual void OnFullStreamCollected(const net::ReadSequence::StreamPtr& stream) override
    {
        m_Streams.push_back(stream);
    }

    std::vector<net::ReadSequence::StreamPtr> m_Streams;
};

TEST(DataSequence, SingleReadWrite)
{
    std::string fullText;
    auto callback = boost::make_shared<CollectorCallback>();
    {
        const auto connection = boost::make_shared<TestConnection>();

        // make data sequence
        {
            net::SequencedConnection stream(connection);

            const std::vector<std::string> data = boost::assign::list_of("data 1 text, \n \n ok")(" \n dsdsa \t bla bla")("data 3 text bla bla bla");
            for (unsigned i = 0; i < data.size(); ++i)
            {
                const auto& d = data[i];
                stream.Prepare(d.size())->Write(d.c_str(), d.size());
                fullText += d;
            }
        }

        // parse sequence
        const auto collector = net::ISequenceCollector::Instance(callback);

        const auto data = connection->m_Data;
        for (const auto& str : data)
        {
            const auto stream = boost::make_shared<std::stringstream>(str);
            collector->OnNewStream(stream);
        }
    }

    ASSERT_FALSE(callback->m_Streams.empty());
    ASSERT_TRUE(callback->m_Streams.front());

    // get final stream
    const net::ReadSequence::StreamPtr stream = callback->m_Streams.front();

    // compare
    std::string result;
    *stream >> std::noskipws;
    std::copy(std::istream_iterator<char>(*stream), std::istream_iterator<char>(), std::back_inserter(result));

    EXPECT_EQ(fullText, result);
}

void GenerateRandom(std::vector<std::string>& data)
{
    static boost::random::mt19937 rng;
    boost::random::uniform_int_distribution<> byte(1, 255);

    boost::random::uniform_int_distribution<> length(10, 50);
    for (unsigned i = 0; i < 10; ++i)
    {
        data.push_back(std::string());

        const auto max = length(rng);
        for (int l = 0 ; l < max; ++l)
            data.back().push_back(static_cast<char>(byte(rng)));
    }
}

TEST(DataSequence, MultipleReadWrite)
{
    std::vector<std::string> fullTexts(100);
    auto callback = boost::make_shared<CollectorCallback>();

    // make data sequence
    {
        const auto connection = boost::make_shared<TestConnection>();

        std::vector<boost::shared_ptr<net::SequencedConnection> > streams;
        for (unsigned i = 0; i < fullTexts.size(); ++i)
            streams.push_back(boost::make_shared<net::SequencedConnection>(connection));

        std::vector<std::vector<std::string>> all(fullTexts.size());
        boost::for_each(all, boost::bind(&GenerateRandom, _1));

        for (unsigned part = 0; part < all.front().size(); ++part)
        {
            for (unsigned streamIndex = 0 ; streamIndex < all.size(); ++streamIndex)
            {
                const std::string& text = all[streamIndex][part];

                streams[streamIndex]->Prepare(text.size())->Write(text.c_str(), text.size());
                fullTexts[streamIndex] += text;
            }
        }

        streams.clear();

        // parse sequence
        const auto collector = net::ISequenceCollector::Instance(callback);

        const auto data = connection->m_Data;
        for (const auto& str : data)
        {
            const auto stream = boost::make_shared<std::stringstream>(str);
            collector->OnNewStream(stream);
        }
    }

    ASSERT_EQ(callback->m_Streams.size(), fullTexts.size());

    for (unsigned i = 0; i < fullTexts.size(); ++i)
    {
        // get final stream
        const net::ReadSequence::StreamPtr stream = callback->m_Streams[i];

        ASSERT_TRUE(stream);

        // compare
        std::string result;
        *stream >> std::noskipws;
        std::copy(std::istream_iterator<char>(*stream), std::istream_iterator<char>(), std::back_inserter(result));

        EXPECT_EQ(fullTexts[i], result);
    }
}
