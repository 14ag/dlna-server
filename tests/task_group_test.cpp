#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "task_group.h"

TEST(TaskGroupTest, WaitReturnsImmediatelyWhenEmpty) {
    TaskGroup group;
    group.Wait();
}

TEST(TaskGroupTest, WaitBlocksUntilAllLeave) {
    TaskGroup group;
    group.Enter();
    group.Enter();
    std::atomic<bool> waited{false};
    std::thread waiter([&]() { group.Wait(); waited = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(waited.load());
    group.Leave();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(waited.load());
    group.Leave();
    waiter.join();
    EXPECT_TRUE(waited.load());
}