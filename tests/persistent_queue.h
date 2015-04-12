#include "log/log.h"
#include "net/details/memory.hpp"
#include "net/details/persistent_queue.hpp"

#include <iostream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/bind.hpp>

using ::testing::_;
using ::testing::Return;
using ::testing::Exactly;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::Expectation;

class SettingsWithSmallLimits
{
public:
    std::size_t GetQueueMaxElemCount() const
    {
        return 2;
    }
    std::size_t GetQueueMaxByteSize() const
    {
        return 1000;
    }
};

typedef net::details::PersistentQueue<net::DefaultSettings> DefaultQueue;
class QueueDefaultCallback
{
public:
    MOCK_METHOD1(Handle, void(const net::MemHolder& h));
};

typedef net::details::PersistentQueue<SettingsWithSmallLimits> SmallQueue;
class SmallQueueCallback
{
public:
    MOCK_METHOD1(Handle, void(const net::MemHolder& h));
};

TEST(Queue, PushFirstImmediately)
{
    QueueDefaultCallback cb;
    const auto func = boost::bind(&QueueDefaultCallback::Handle, &cb, _1);
    DefaultQueue queue;

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(1));

    const auto mem = boost::make_shared_noinit<char[]>(1);
    net::MemHolder holder {mem, 1};
    queue.Push(std::move(holder), func);

    ASSERT_FALSE(queue.IsEmpty());
}

TEST(Queue, PushAnotherAfterPop)
{
    QueueDefaultCallback cb;
    const auto func = boost::bind(&QueueDefaultCallback::Handle, &cb, _1);
    DefaultQueue queue;

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(2));

    const auto mem = boost::make_shared_noinit<char[]>(1);
    net::MemHolder holder {mem, 1};
    queue.Push(std::move(holder), func);
    queue.Push(std::move(holder), func);

    queue.Pop(func);

    ASSERT_FALSE(queue.IsEmpty());
    queue.Pop(func);
    ASSERT_TRUE(queue.IsEmpty());
}

TEST(Queue, Persist)
{
    SmallQueueCallback cb;
    SmallQueue queue;
    const auto func = boost::bind(&SmallQueueCallback::Handle, &cb, _1);

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(3));

    for (int i = 0; i < 3; ++i)
    {
        const auto mem = boost::make_shared_noinit<char[]>(1);
        net::MemHolder holder {mem, 1};
        queue.Push(std::move(holder), func);
    }

    for (int i = 0; i < 3; ++i)
        queue.Pop(func);

    ASSERT_TRUE(queue.IsEmpty());
}
