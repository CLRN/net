//#include "log/log.h"
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

class SettingsWithPersistency
{
public:
    static bool IsPersistent()
    {
        return true;
    }
};

typedef net::details::PersistentQueue<net::DefaultSettings> DefaultQueue;
class QueueDefaultCallback
{
public:
    MOCK_METHOD1(Handle, void(const net::details::MemoryPool::Buffers& b));
};

typedef net::details::PersistentQueue<SettingsWithPersistency> SmallQueue;
class SmallQueueCallback
{
public:
    MOCK_METHOD1(Handle, void(const net::details::MemoryPool::Buffers& b));
};

TEST(Queue, PushFirstImmediately)
{
    QueueDefaultCallback cb;
    const auto func = boost::bind(&QueueDefaultCallback::Handle, &cb, _1);
    DefaultQueue queue(func);

    const auto data = queue.Prepare(1);

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(1));
    data->Write("1", 1);
}

TEST(Queue, WriteWhenDataComplete)
{
    QueueDefaultCallback cb;
    const auto func = boost::bind(&QueueDefaultCallback::Handle, &cb, _1);
    DefaultQueue queue(func);

    {
        const auto data = queue.Prepare(2);
        data->Write("1", 1);

        EXPECT_CALL(cb, Handle(_)).Times(Exactly(1));
        data->Write("2", 1);
    }

    ASSERT_FALSE(queue.IsEmpty());
    queue.OnWriteCompleted(2);
    ASSERT_TRUE(queue.IsEmpty());
}


TEST(Queue, WriteAfterCommit)
{
    QueueDefaultCallback cb;
    const auto func = boost::bind(&QueueDefaultCallback::Handle, &cb, _1);
    DefaultQueue queue(func);

    {
        const auto data = queue.Prepare(1);
        EXPECT_CALL(cb, Handle(_)).Times(Exactly(1));
        data->Write("1", 1);
    }

    {
        const auto data = queue.Prepare(1);
        data->Write("1", 1);


        EXPECT_CALL(cb, Handle(_)).Times(Exactly(1));
        queue.OnWriteCompleted(1);
    }

    ASSERT_FALSE(queue.IsEmpty());
    queue.OnWriteCompleted(1);
    ASSERT_TRUE(queue.IsEmpty());
}

TEST(Queue, Persist)
{
    SmallQueueCallback cb;
    const auto func = boost::bind(&SmallQueueCallback::Handle, &cb, _1);
    SmallQueue queue(func);

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(3));

    for (int i = 0; i < 3; ++i)
    {
        queue.Prepare(1)->Write("1", 1);
        queue.OnWriteCompleted(1);
    }
    queue.UpdateAcknowledgedBytes(3);

    ASSERT_TRUE(queue.IsEmpty());
}
