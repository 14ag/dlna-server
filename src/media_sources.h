#ifndef MEDIA_SOURCES_H
#define MEDIA_SOURCES_H

#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <deque>
#include <memory>

#include "task_group.h"
#include "playlist_scan_concurrency.h"
#include "config.h"

class MediaDatabase;

struct MediaItem {
    int id;
    int parentId;
    std::wstring path;
    std::wstring title;
    bool isFolder;
    std::wstring mimeType;
    std::wstring upnpClass;
    long long sizeBytes;
    // full path to companion subtitle file if one exists in the same folder
    // empty string means no subtitle was found
    std::wstring subtitlePath;
    std::wstring albumArtPath;
    std::wstring albumArtMime;
    // YYYY-MM-DD date for dc:date element; empty means omit
    std::string dcDate;
    // millisecond epoch for rawDate attribute on <item>; 0 means omit
    long long rawDateMs = 0;
};

struct MediaIndexState;
struct PlaylistScanContext;

struct PendingPlaylistNode {
    std::wstring path;
    int parentId;
    std::wstring titleOverride;
    int depth;
};

struct MediaIndexState {
    std::vector<MediaItem> items;
    std::unordered_map<int, size_t> idToIndex;
    std::unordered_map<int, std::vector<size_t>> childrenByParent;
    std::unordered_map<std::wstring, int> containerKeys;
    std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> folderAlbumArt;
    std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> perStemAlbumArt;
    std::unordered_set<std::wstring> duplicateKeys;
    MediaDatabase* mediaDatabase = nullptr;

    std::unique_ptr<std::mutex> mutationMutex = std::make_unique<std::mutex>();
};

constexpr int kMaxPlaylistRecursionDepth = 8;

class MediaSources {
public:
    static MediaSources& Get();

    void Scan();
    std::vector<MediaItem> GetDescendants(int parentId);
    MediaItem GetItem(int id);
    std::unordered_map<int, int> GetChildCounts(const std::vector<MediaItem>& items);
    int GetSystemUpdateID();

    enum class GetChildrenResult {
        Success,
        NotFound,
        NotAContainer
    };
    GetChildrenResult TryGetChildren(int objId, std::vector<MediaItem>& out);

    void ResetForRescan();
    int PublishContainer(MediaDatabase* database, int parentId,
                          const std::wstring& title, const std::wstring& path,
                          std::function<std::wstring(const std::wstring&)> canonicalize);
    void PublishItem(MediaItem item);

private:
    MediaSources();
    void AddMediaFile(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& path, int parentId, const std::wstring& titleOverride = L"", const std::wstring& subtitleOverride = L"", bool allowArtistAlbumMirror = true);
    void AddHlsStreamItem(MediaIndexState& state, const std::wstring& path, int parentId, const std::wstring& titleOverride = L"");

public:
    // Entry point used by Scan() (Task 9) and, internally, by ScanFolder /
    // ScanNetworkFolder when they discover a playlist file mid-walk. Enqueues
    // the root node and runs the dispatcher until every node discovered
    // transitively under `path` has been scanned and published. Blocking:
    // call this from a background scan task, never from Server::Start()'s
    // calling thread directly (see Task 9/10).
    void ScanPlaylistTree(std::shared_ptr<PlaylistScanContext> ctx, const std::wstring& path,
                          int parentId, const std::wstring& titleOverride = L"");

private:
    void ScanNetworkFolder(std::shared_ptr<PlaylistScanContext> sourceContext, const std::wstring& folderUrl, int parentId, int depth);
    void ScanFolder(std::shared_ptr<PlaylistScanContext> sourceContext, const std::wstring& rootPath, int parentId, int depth = 0);
    bool IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass);
    void RunPlaylistDispatcher(std::shared_ptr<PlaylistScanContext> ctx);
    void ScanOnePlaylistNode(std::shared_ptr<PlaylistScanContext> ctx, const PendingPlaylistNode& node, TaskGroupLeaveGuard& guard);

    std::mutex m_mutex;
    std::vector<MediaItem> m_items;
    std::unordered_map<int, size_t> m_idToIndex;
    std::unordered_map<int, std::vector<size_t>> m_childrenByParent;
    std::atomic<int> m_systemUpdateId;
};

// Owns everything needed to scan one top-level media source's playlist/HLS
// subtree concurrently. One instance per configured source; shared by every
// playlist reference discovered anywhere under that source (see Task 7
// background). Lives as long as the source's scan is in flight -- keep it
// alive via std::shared_ptr captured into every submitted task.
struct PlaylistScanContext {
    ConfigSnapshot cfg;
    MediaIndexState state;                    // per-source scratch caches, see Task 5
    AdaptiveConcurrencyLimiter limiter{1};
    std::atomic<size_t> discoveredCount{0};
    TaskGroup group;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<PendingPlaylistNode> pendingQueue;
};

#define AppMedia MediaSources::Get()

#endif // MEDIA_SOURCES_H
