#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "bounded_thread_pool.h"
#include "task_group.h"

TEST(BoundedThreadPoolTest, RunsAllSubmittedTasks) {
    BoundedThreadPool pool(4);
    std::atomic<int> counter{0};
    TaskGroup group;
    for (int i = 0; i < 100; ++i) {
        group.Enter();
        pool.Submit([&]() {
            TaskGroupLeaveGuard leave(group);
            ++counter;
        });
    }
    group.Wait();
    EXPECT_EQ(counter.load(), 100);
}

TEST(BoundedThreadPoolTest, NeverExceedsWorkerCount) {
    BoundedThreadPool pool(4);
    std::atomic<int> active{0};
    std::atomic<int> maxObserved{0};
    TaskGroup group;
    for (int i = 0; i < 20; ++i) {
        group.Enter();
        pool.Submit([&]() {
            TaskGroupLeaveGuard leave(group);
            int now = ++active;
            int prevMax = maxObserved.load();
            while (now > prevMax && !maxObserved.compare_exchange_weak(prevMax, now)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            --active;
        });
    }
    group.Wait();
    EXPECT_LE(maxObserved.load(), 4);
}