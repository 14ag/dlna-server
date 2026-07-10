#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <unordered_set>
#include "media_database.h"

TEST(MediaDatabaseThreadSafetyTest, ConcurrentGetOrCreateStableIdIsRace_Free) {
    MediaDatabase db;
    constexpr int kThreads = 16;
    constexpr int kKeysPerThread = 200;
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&db, t]() {
            for (int i = 0; i < kKeysPerThread; ++i) {
                std::wstring key = L"key-" + std::to_wstring(t) + L"-" + std::to_wstring(i);
                db.GetOrCreateStableId(key);
            }
        });
    }
    for (auto& w : workers) w.join();
    std::unordered_set<int> seen;
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kKeysPerThread; ++i) {
            std::wstring key = L"key-" + std::to_wstring(t) + L"-" + std::to_wstring(i);
            int id = db.GetOrCreateStableId(key);
            EXPECT_TRUE(seen.insert(id).second) << "duplicate id for " << i;
        }
    }
}