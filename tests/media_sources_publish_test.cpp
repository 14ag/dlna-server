#include <gtest/gtest.h>
#include "media_sources.h"

TEST(MediaSourcesPublishTest, PublishContainerIsImmediatelyVisible) {
    MediaSources::Get().ResetForRescan();
    int id = MediaSources::Get().PublishContainer(nullptr, 0, L"Test", L"C:\\test",
        [](const std::wstring& s) { return s; });
    MediaItem item = MediaSources::Get().GetItem(id);
    EXPECT_EQ(item.id, id);
    EXPECT_EQ(item.title, L"Test");
    std::vector<MediaItem> children;
    auto status = MediaSources::Get().TryGetChildren(0, children);
    EXPECT_EQ(status, MediaSources::GetChildrenResult::Success);
    EXPECT_EQ(children.size(), 1u);
}

TEST(MediaSourcesPublishTest, PublishItemIsImmediatelyVisibleUnderParent) {
    MediaSources::Get().ResetForRescan();
    int folderId = MediaSources::Get().PublishContainer(nullptr, 0, L"Folder", L"p",
        [](const std::wstring& s) { return s; });
    MediaItem file{};
    file.id = 12345;
    file.parentId = folderId;
    file.title = L"song.mp3";
    file.isFolder = false;
    MediaSources::Get().PublishItem(file);
    std::vector<MediaItem> children;
    MediaSources::Get().TryGetChildren(folderId, children);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0].id, 12345);
}

TEST(MediaSourcesPublishTest, ResetForRescanClearsAndRepublishesRootOnly) {
    MediaSources::Get().ResetForRescan();
    MediaSources::Get().PublishContainer(nullptr, 0, L"Stale", L"p",
        [](const std::wstring& s) { return s; });
    MediaSources::Get().ResetForRescan();
    MediaItem root = MediaSources::Get().GetItem(0);
    EXPECT_EQ(root.title, L"Root");
    std::vector<MediaItem> children;
    MediaSources::Get().TryGetChildren(0, children);
    EXPECT_TRUE(children.empty());
}