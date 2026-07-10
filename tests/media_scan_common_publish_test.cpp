#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "media_sources.h"
#include "media_scan_common.h"

TEST(FindOrAddContainerTest, SameKeyReturnsSameIdWithoutDuplicatePublish) {
    MediaSources::Get().ResetForRescan();
    MediaIndexState state;
    auto canon = [](const std::wstring& s) { return s; };
    int id1 = FindOrAddContainer(state, 0, L"Artist", L"C:\\music\\artist", canon);
    int id2 = FindOrAddContainer(state, 0, L"Artist", L"C:\\music\\artist", canon);
    EXPECT_EQ(id1, id2);
    std::vector<MediaItem> children;
    MediaSources::Get().TryGetChildren(0, children);
    EXPECT_EQ(children.size(), 1u);
}

TEST(FindOrAddContainerTest, ConcurrentCallsForSameKeyProduceOneContainer) {
    MediaSources::Get().ResetForRescan();
    MediaIndexState state;
    auto canon = [](const std::wstring& s) { return s; };
    std::vector<std::thread> workers;
    std::vector<int> ids(8);
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&, i]() {
            ids[i] = FindOrAddContainer(state, 0, L"Same", L"C:\\same", canon);
        });
    }
    for (auto& w : workers) w.join();
    for (int id : ids) EXPECT_EQ(id, ids[0]);
    std::vector<MediaItem> children;
    MediaSources::Get().TryGetChildren(0, children);
    EXPECT_EQ(children.size(), 1u);
}