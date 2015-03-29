#include "log/log.h"
#include "net/details/allocator.hpp"
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
    static std::size_t GetQueueMaxElemCount()
    {
        return 2;
    }
    static std::size_t GetQueueMaxByteSize()
    {
        return 1000;
    }
};

typedef net::details::PersistentQueue<net::CrtAllocator> DefaultQueue;
class QueueDefaultCallback
{
public:
    MOCK_METHOD1(Handle, void(const DefaultQueue::MemHolder& h));
};

typedef net::details::PersistentQueue<net::CrtAllocator, SettingsWithSmallLimits> SmallQueue;
class SmallQueueCallback
{
public:
    MOCK_METHOD1(Handle, void(const SmallQueue::MemHolder& h));
};

TEST(Queue, PushFirstImmediately)
{
    net::CrtAllocator allocator;

    QueueDefaultCallback cb;
    DefaultQueue queue(boost::bind(&QueueDefaultCallback::Handle, &cb, _1));

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(1));

    const auto mem = allocator.Allocate(1);
    DefaultQueue::MemHolder holder {mem, 1};
    queue.Push(std::move(holder));

    ASSERT_FALSE(queue.IsEmpty());
}

TEST(Queue, PushAnotherAfterPop)
{
    net::CrtAllocator allocator;

    QueueDefaultCallback cb;
    DefaultQueue queue(boost::bind(&QueueDefaultCallback::Handle, &cb, _1));

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(2));

    const auto mem = allocator.Allocate(1);
    DefaultQueue::MemHolder holder {mem, 1};
    queue.Push(std::move(holder));
    queue.Push(std::move(holder));

    queue.Pop();

    ASSERT_FALSE(queue.IsEmpty());
    queue.Pop();
    ASSERT_TRUE(queue.IsEmpty());
}

TEST(Queue, Persist)
{
    net::CrtAllocator allocator;

    SmallQueueCallback cb;
    SmallQueue queue(boost::bind(&SmallQueueCallback::Handle, &cb, _1));

    EXPECT_CALL(cb, Handle(_)).Times(Exactly(3));

    for (int i = 0; i < 3; ++i)
    {
        const auto mem = allocator.Allocate(1);
        SmallQueue::MemHolder holder {mem, 1};
        queue.Push(std::move(holder));
    }

    for (int i = 0; i < 3; ++i)
        queue.Pop();

    ASSERT_TRUE(queue.IsEmpty());
}

GTEST_API_ int main(int argc, char **argv)
{
    std::cout << "Running main() from gtest_main.cc\n";

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}