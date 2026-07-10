#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include "playlist_scan_concurrency.h"

TEST(PlaylistScanConcurrencyTest, EndpointsMatchSpec) {
    EXPECT_EQ(ComputePlaylistScanConcurrency(0), 1u);
    EXPECT_EQ(ComputePlaylistScanConcurrency(1), 1u);
    EXPECT_EQ(ComputePlaylistScanConcurrency(200), 20u);
    EXPECT_EQ(ComputePlaylistScanConcurrency(201), 20u);
    EXPECT_EQ(ComputePlaylistScanConcurrency(100000), 20u);
}

TEST(PlaylistScanConcurrencyTest, MonotonicNonDecreasing) {
    size_t previous = ComputePlaylistScanConcurrency(1);
    for (size_t n = 2; n <= 400; ++n) {
        size_t current = ComputePlaylistScanConcurrency(n);
        EXPECT_GE(current, previous);
        previous = current;
    }
}

TEST(AdaptiveConcurrencyLimiterTest, NeverExceedsCurrentLimit) {
    AdaptiveConcurrencyLimiter limiter(3);
    std::atomic<int> active{0}, maxObserved{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 12; ++i) {
        workers.emplace_back([&]() {
            limiter.Acquire();
            int now = ++active;
            int prevMax = maxObserved.load();
            while (now > prevMax && !maxObserved.compare_exchange_weak(prevMax, now)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            --active;
            limiter.Release();
        });
    }
    for (auto& t : workers) t.join();
    EXPECT_LE(maxObserved.load(), 3);
}

TEST(AdaptiveConcurrencyLimiterTest, RaisingLimitUnblocksWaiters) {
    AdaptiveConcurrencyLimiter limiter(1);
    limiter.Acquire();
    std::atomic<bool> acquired{false};
    std::thread waiter([&]() { limiter.Acquire(); acquired = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(acquired.load());
    limiter.SetLimit(2);
    waiter.join();
    EXPECT_TRUE(acquired.load());
}

TEST(PlaylistScanPoolTest, ReturnsFixedTwentyWorkerPool) {
    EXPECT_EQ(PlaylistScanPool::Get().WorkerCount(), kPlaylistScanPoolSize);
}