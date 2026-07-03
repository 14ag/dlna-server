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

int AllocateContainerId(MediaIndexState& state, int parentId, const std::wstring& title,
                        const std::wstring& keyPath,
                        std::function<std::wstring(const std::wstring&)> canonicalize) {
    if (state.mediaDatabase) {
        return state.mediaDatabase->GetOrCreateStableContainerId(
            BuildStableContainerKey(parentId, title, keyPath, canonicalize));
    }
    return state.nextId++;
}

ScopedScanSuccess::ScopedScanSuccess(MediaDatabase* database, std::wstring key)
    : database(database), key(std::move(key)), marked(false) {}

void ScopedScanSuccess::Mark() {
    if (database && !marked) {
        database->MarkScanSuccess(key);
        marked = true;
    }
}

int FindOrAddContainer(MediaIndexState& state, int parentId, const std::wstring& title,
                       const std::wstring& keyPath,
                       std::function<std::wstring(const std::wstring&)> canonicalize) {
    const std::wstring lookupKey = ContainerLookupKey(parentId, title, keyPath);
    auto found = state.containerKeys.find(lookupKey);
    if (found != state.containerKeys.end()) {
        return found->second;
    }

    MediaItem folderInfo;
    folderInfo.id = AllocateContainerId(state, parentId, title, keyPath, canonicalize);
    folderInfo.parentId = parentId;
    folderInfo.path = keyPath;
    folderInfo.title = title;
    folderInfo.isFolder = true;
    folderInfo.upnpClass = L"object.container.storageFolder";
    state.items.push_back(folderInfo);
    state.containerKeys[lookupKey] = folderInfo.id;
    return folderInfo.id;
}

void AppendDescendants(const std::vector<MediaItem>& items,
                       const std::unordered_map<int, std::vector<size_t>>& childrenByParent,
                       int parentId,
                       bool sortByTitle,
                       std::vector<MediaItem>& result) {
    auto found = childrenByParent.find(parentId);
    if (found == childrenByParent.end()) return;

    std::vector<MediaItem> children;
    for (size_t index : found->second) {
        if (index < items.size()) children.push_back(items[index]);
    }
    if (sortByTitle) {
        std::sort(children.begin(), children.end(), [](const MediaItem& a, const MediaItem& b) {
            if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
            return NaturalLessWide(a.title, b.title);
        });
    }

    for (const auto& child : children) {
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
    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(albumId, item.path, canonicalize)).second) return;

    MediaItem mirror = item;
    mirror.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(BuildStableMediaKey(albumId, item.path, canonicalize))
        : state.nextId++;
    mirror.parentId = albumId;
    state.items.push_back(mirror);
}