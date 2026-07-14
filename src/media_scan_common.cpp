#include "media_scan_common.h"
#include "config.h"
#include "dlna_utils.h"
#include "media_database.h"
#include "media_sources.h"
#include "network_sources.h"
#include <algorithm>
#include <utility>
#include <vector>

std::wstring ContainerLookupKey(int parentId, const std::wstring& title, const std::wstring& keyPath) {
    return std::to_wstring(parentId) + L"\n" + title + L"\n" + keyPath;
}

std::wstring BuildStableContainerKey(int parentId, const std::wstring& title, const std::wstring& keyPath,
                                     std::function<std::wstring(const std::wstring&)> canonicalize) {
    return L"container\n" + ContainerLookupKey(parentId, title, canonicalize(keyPath));
}

std::wstring BuildDuplicateMediaKey(int parentId, const std::wstring& path,
                                    std::function<std::wstring(const std::wstring&)> canonicalize) {
    return L"media\n" + std::to_wstring(parentId) + L"\n" + canonicalize(path);
}

std::wstring BuildStableMediaKey(int parentId, const std::wstring& path,
                                 std::function<std::wstring(const std::wstring&)> canonicalize) {
    return BuildDuplicateMediaKey(parentId, path, canonicalize);
}

int FindOrAddContainer(MediaIndexState& state, int parentId, const std::wstring& title,
                       const std::wstring& keyPath,
                       std::function<std::wstring(const std::wstring&)> canonicalize) {
    const std::wstring lookupKey = ContainerLookupKey(parentId, title, keyPath);
    std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
    auto found = state.containerKeys.find(lookupKey);
    if (found != state.containerKeys.end()) return found->second;
    const int id = AppMedia.PublishContainer(state.mediaDatabase, parentId, title, keyPath, canonicalize);
    state.containerKeys[lookupKey] = id;
    return id;
}

ScanSuccessMarker::ScanSuccessMarker(MediaDatabase* database, std::wstring key)
    : database(database), key(std::move(key)), marked(false) {}

void ScanSuccessMarker::Mark() {
    if (database && !marked) {
        database->MarkScanSuccess(key);
        marked = true;
    }
}

void AppendDescendants(const std::vector<MediaItem>& items,
                       const std::unordered_map<int, std::vector<size_t>>& childrenByParent,
                       int parentId,
                       bool sortByTitle,
                       std::vector<MediaItem>& result) {
    auto found = childrenByParent.find(parentId);
    if (found == childrenByParent.end()) return;

    std::vector<size_t> childIndices;
    childIndices.reserve(found->second.size());
    for (size_t index : found->second) {
        if (index < items.size()) childIndices.push_back(index);
    }
    if (sortByTitle) {
        std::sort(childIndices.begin(), childIndices.end(), [&items](size_t a, size_t b) {
            const MediaItem& left = items[a];
            const MediaItem& right = items[b];
            if (left.isFolder != right.isFolder) return left.isFolder && !right.isFolder;
            return NaturalLessWide(left.title, right.title);
        });
    }

    for (size_t index : childIndices) {
        const MediaItem& child = items[index];
        result.push_back(child);
        if (child.isFolder) AppendDescendants(items, childrenByParent, child.id, sortByTitle, result);
    }
}

void AddArtistAlbumMirrorIfPresent(MediaIndexState& state, const ConfigSnapshot& cfg,
                                   const MediaItem& item, int sourceParentId,
                                   std::function<std::wstring(const std::wstring&)> parentPathOf,
                                   std::function<std::wstring(const std::wstring&)> nameOfPath,
                                   std::function<std::wstring(const std::wstring&)> canonicalize) {
    if (!cfg.addArtistAlbumFolders || IsRemoteMediaUrl(item.path)) return;
    if (item.upnpClass != L"object.item.audioItem.musicTrack" && item.upnpClass != L"object.item.videoItem") return;

    const std::wstring albumPath = parentPathOf(item.path);
    const std::wstring artistPath = parentPathOf(albumPath);
    const std::wstring album = nameOfPath(albumPath);
    const std::wstring artist = nameOfPath(artistPath);
    if (artist.empty() || album.empty()) return;

    int artistId = FindOrAddContainer(state, sourceParentId, artist, artistPath, canonicalize);
    int albumId = FindOrAddContainer(state, artistId, album, albumPath, canonicalize);
    {
        std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
        if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(albumId, item.path, canonicalize)).second) return;
    }
    MediaItem mirror = item;
    mirror.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(BuildStableMediaKey(albumId, item.path, canonicalize))
        : 0;
    mirror.parentId = albumId;
    AppMedia.PublishItem(std::move(mirror));
}