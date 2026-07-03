#ifndef MEDIA_SCAN_COMMON_H
#define MEDIA_SCAN_COMMON_H

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct ConfigSnapshot;
class MediaDatabase;
struct MediaItem;
struct MediaIndexState;

std::wstring ContainerLookupKey(int parentId, const std::wstring& title, const std::wstring& keyPath);

std::wstring BuildStableContainerKey(int parentId, const std::wstring& title, const std::wstring& keyPath,
                                     std::function<std::wstring(const std::wstring&)> canonicalize);

std::wstring BuildDuplicateMediaKey(int parentId, const std::wstring& path,
                                    std::function<std::wstring(const std::wstring&)> canonicalize);

std::wstring BuildStableMediaKey(int parentId, const std::wstring& path,
                                 std::function<std::wstring(const std::wstring&)> canonicalize);

int AllocateContainerId(MediaIndexState& state, int parentId, const std::wstring& title,
                        const std::wstring& keyPath,
                        std::function<std::wstring(const std::wstring&)> canonicalize);

struct ScopedScanSuccess {
    ScopedScanSuccess(MediaDatabase* database, std::wstring key);
    void Mark();
    MediaDatabase* database;
    std::wstring key;
    bool marked;
};

int FindOrAddContainer(MediaIndexState& state, int parentId, const std::wstring& title,
                       const std::wstring& keyPath,
                       std::function<std::wstring(const std::wstring&)> canonicalize);

void AppendDescendants(const std::vector<MediaItem>& items,
                       const std::unordered_map<int, std::vector<size_t>>& childrenByParent,
                       int parentId, bool sortByTitle, std::vector<MediaItem>& result);

void AddArtistAlbumMirrorIfPresent(MediaIndexState& state, const ConfigSnapshot& cfg,
                                   const MediaItem& item, int sourceParentId,
                                   std::function<std::wstring(const std::wstring&)> parentPathOf,
                                   std::function<std::wstring(const std::wstring&)> nameOfPath,
                                   std::function<std::wstring(const std::wstring&)> canonicalize);

#endif // MEDIA_SCAN_COMMON_H