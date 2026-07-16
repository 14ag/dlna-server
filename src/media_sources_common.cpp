#include "media_sources.h"
#include "fs_backend.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_database.h"
#include "media_scan_common.h"
#include "network_sources.h"
#include "playlist_scan_concurrency.h"
#include "task_group.h"
#include "upnp_eventing.h"
#include <shared_mutex>
#include <algorithm>
#include <ctime>
#include <functional>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#endif

namespace {

constexpr const wchar_t* kScanDepthLogCode = L"[media:scan-depth]";

bool DefaultPlaylistFileExists(const std::wstring& path) {
    return FsIsRegularFile(path);
}

std::wstring CanonicalMediaKey(const std::wstring& path) {
    if (IsRemoteMediaUrl(path)) return ToLowerWide(path);
    return ToLowerWide(path);
}

const auto g_canonicalize = [](const std::wstring& path) -> std::wstring {
    return CanonicalMediaKey(path);
};

// Single shared source of negative scratch IDs used whenever no
// MediaDatabase is available to hand out a stable persistent ID  Every
// fallback-ID call site in this file must go through this one counter
// previously each call site kept its own independently-seeded static
// atomic (-2 -1000002 -2000002) which only avoided collisions because
// someone hand-picked disjoint ranges  A single shared strictly
// decreasing counter cannot collide with itself by construction
int NextScratchId() {
    static std::atomic<int> s_nextScratchId{-2};
    return s_nextScratchId.fetch_sub(1, std::memory_order_relaxed);
}

std::wstring ParentPathOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

std::wstring NameOfPath(const std::wstring& path) {
    std::wstring clean = path;
    while (!clean.empty() && (clean.back() == L'\\' || clean.back() == L'/')) clean.pop_back();
    size_t slash = clean.find_last_of(L"\\/");
    return slash == std::wstring::npos ? clean : clean.substr(slash + 1);
}

void SetAlbumArtIfExists(MediaIndexState& state, MediaItem& item) {
    if (IsRemoteMediaUrl(item.path)) return;
    std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
    std::wstring folder = item.path;
    size_t slash = folder.find_last_of(L"\\/");
    folder = slash == std::wstring::npos ? L"." : folder.substr(0, slash);

    std::wstring fileName = SourceDisplayName(item.path);
    std::wstring stem = SourceStemName(fileName);
    const std::wstring stemKey = folder + L"\n" + stem;

    auto stemCached = state.perStemAlbumArt.find(stemKey);
    if (stemCached != state.perStemAlbumArt.end()) {
        item.albumArtPath = stemCached->second.first;
        item.albumArtMime = stemCached->second.second;
        return;
    }

    std::vector<AlbumArtCandidate> perStemCandidates = {
        { stem + L".jpg", L"image/jpeg" },
        { stem + L".jpeg", L"image/jpeg" },
        { stem + L".png", L"image/png" },
    };
    for (const auto& candidate : perStemCandidates) {
        std::wstring candidatePath = folder + L"\\" + candidate.fileName;
        if (FsIsRegularFile(candidatePath)) {
            item.albumArtPath = candidatePath;
            item.albumArtMime = candidate.mimeType;
            state.perStemAlbumArt[stemKey] = { item.albumArtPath, item.albumArtMime };
            return;
        }
    }
    state.perStemAlbumArt[stemKey] = { L"", L"" };

    auto folderCached = state.folderAlbumArt.find(folder);
    if (folderCached != state.folderAlbumArt.end()) {
        item.albumArtPath = folderCached->second.first;
        item.albumArtMime = folderCached->second.second;
        return;
    }
    for (const auto& candidate : BuildAlbumArtCandidateNames(L"")) {
        std::wstring candidatePath = folder + L"\\" + candidate.fileName;
        if (FsIsRegularFile(candidatePath)) {
            item.albumArtPath = candidatePath;
            item.albumArtMime = candidate.mimeType;
            state.folderAlbumArt[folder] = { item.albumArtPath, item.albumArtMime };
            return;
        }
    }
    state.folderAlbumArt[folder] = { L"", L"" };
}

} // namespace

MediaSources& MediaSources::Get() {
    static MediaSources instance;
    return instance;
}

MediaSources::MediaSources() : m_systemUpdateId(1) {
}

bool MediaSources::IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass) {
    MediaFormatInfo info;
    if (!GetMediaFormatForExtension(ext, info)) return false;
    mime = info.mimeType;
    uclass = info.upnpClass;
    return true;
}

void MediaSources::Scan() {
    
    const ConfigSnapshot cfg = AppConfig.Snapshot();
    auto database = std::make_shared<MediaDatabase>();
    database->Load(MediaDatabase::DefaultDatabasePath());

    struct SourceJob {
        std::shared_ptr<PlaylistScanContext> ctx;
        MediaSource source;
        int containerId;
    };
    std::vector<SourceJob> jobs;

    for (const auto& src : cfg.effectiveMediaSources) {
        if (IsRemovedSmbSourcePath(src.path)) {
            LogPrint(L"[media:smb-removed] SMB media sources are no longer supported; skipping: %ls",
                     RedactUrlForLog(src.path).c_str());
            continue;
        }
        if (!IsPlaylistSourcePath(src.path) && !IsNetworkShareUrl(src.path)) {
            if (!FsExists(src.path)) {
                database->RecordScanError(CanonicalMediaKey(src.path), L"Source unavailable");
                LogPrint(L"Skipping missing source: %ls", src.path.c_str());
                continue;
            }
        }
        const int containerId = PublishContainer(database.get(), 0, SourceDisplayName(src.path), src.path, g_canonicalize);

        auto ctx = std::make_shared<PlaylistScanContext>();
        ctx->cfg = cfg;
        ctx->state.mediaDatabase = database.get();
        jobs.push_back({ctx, src, containerId});
    }

    if (!cfg.hasRuntimeSourceOverride && cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty() && DefaultPlaylistFileExists(cfg.defaultPlaylistPath)) {
        const int containerId = PublishContainer(database.get(), 0, L"Default playlist", cfg.defaultPlaylistPath, g_canonicalize);
        auto ctx = std::make_shared<PlaylistScanContext>();
        ctx->cfg = cfg;
        ctx->state.mediaDatabase = database.get();
        jobs.push_back({ctx, MediaSource{cfg.defaultPlaylistPath}, containerId});
    }

    TaskGroup sourceGroup;
    for (auto& job : jobs) {
        sourceGroup.Enter();
        PlaylistScanPool::Get().Submit([this, &job, &sourceGroup]() {
            TaskGroupLeaveGuard leave(sourceGroup);
            LogPrint(L"Scanning media source: %ls", RedactUrlForLog(job.source.path).c_str());
            if (IsPlaylistSourcePath(job.source.path)) {
                ScanPlaylistTree(job.ctx, job.source.path, job.containerId);
            } else if (IsNetworkShareUrl(job.source.path)) {
                ScanNetworkFolder(job.ctx, job.source.path, job.containerId, 0);
            } else {
                ScanFolder(job.ctx, job.source.path, job.containerId, 0);
            }
            LogPrint(L"Finished scanning media source: %ls", RedactUrlForLog(job.source.path).c_str());
        });
    }
    sourceGroup.Wait();

    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
    if (!database->Save(MediaDatabase::DefaultDatabasePath())) {
        LogPrint(L"Media database save failed: %ls", MediaDatabase::DefaultDatabasePath().c_str());
    }
    LogPrint(L"Scan complete.");
}

void MediaSources::AddMediaFile(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& path, int parentId, const std::wstring& titleOverride, const std::wstring& subtitleOverride, bool allowArtistAlbumMirror) {
    std::wstring mime, uclass;
    std::wstring ext = SourceExtension(path);
    if (!IsAllowedExtension(ext, mime, uclass)) {
        if (IsRemoteMediaUrl(path) && ext.empty()) {
            mime = L"audio/mpeg";
            uclass = L"object.item.audioItem.musicTrack";
        } else {
            LogPrint(L"[media:reject-extension] Skipping media with unsupported extension '%ls': %ls", ext.c_str(), RedactUrlForLog(path).c_str());
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
        if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, path, g_canonicalize)).second) return;
    }

    MediaItem fileInfo;
    const std::wstring stableKey = BuildStableMediaKey(parentId, path, g_canonicalize);
    ScanSuccessMarker scanSuccess(state.mediaDatabase, stableKey);
    fileInfo.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : NextScratchId();
    fileInfo.parentId = parentId;
    fileInfo.path = path;
    fileInfo.isFolder = false;
    fileInfo.mimeType = mime;
    fileInfo.upnpClass = uclass;

    if (IsRemoteMediaUrl(path)) {
        fileInfo.sizeBytes = ProbeRemoteContentLength(path);
    } else {
        long long size = 0;
        if (!FsFileSize(path, size)) {
            return;
        }
        fileInfo.sizeBytes = size;
    }

    if (cfg.showFileNamesInsteadOfTitles) {
        fileInfo.title = SourceDisplayName(path);
    } else if (!titleOverride.empty()) {
        fileInfo.title = titleOverride;
    } else {
        fileInfo.title = SourceStemName(path);
    }

    if (!subtitleOverride.empty()) {
        fileInfo.subtitlePath = subtitleOverride;
        if (IsRemoteMediaUrl(subtitleOverride)) {
            LogPrint(L"Playlist subtitle resolved to remote URL: %ls", RedactUrlForLog(subtitleOverride).c_str());
        }
    } else if (!IsRemoteMediaUrl(path) && uclass == L"object.item.videoItem") {
        std::wstring stem = SourceStemName(path);

        std::wstring folder = path;
        size_t slash = folder.find_last_of(L"\\/");
        folder = slash == std::wstring::npos ? L"." : folder.substr(0, slash);

        static const wchar_t* kSubExts[] = { L".srt", L".vtt", L".sub", L".ass", L".ssa", L".smi", L".txt" };
        for (const wchar_t* subExt : kSubExts) {
            std::wstring candidate = folder + L"\\" + stem + subExt;
            if (FsIsRegularFile(candidate)) {
                fileInfo.subtitlePath = candidate;
                break;
            }
        }
    }

    SetAlbumArtIfExists(state, fileInfo);
    AppMedia.PublishItem(fileInfo);
    scanSuccess.Mark();
    if (allowArtistAlbumMirror) {
        AddArtistAlbumMirrorIfPresent(state, cfg, fileInfo, parentId, ParentPathOf, NameOfPath, g_canonicalize);
    }
}

void MediaSources::AddHlsStreamItem(MediaIndexState& state, const std::wstring& path, int parentId, const std::wstring& titleOverride) {
    {
        std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
        if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, path, g_canonicalize)).second) return;
    }

    MediaItem hlsItem;
    const std::wstring stableKey = BuildStableMediaKey(parentId, path, g_canonicalize);
    ScanSuccessMarker scanSuccess(state.mediaDatabase, stableKey);
    hlsItem.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : NextScratchId();
    hlsItem.parentId = parentId;
    hlsItem.path = path;
    hlsItem.isFolder = false;
    hlsItem.mimeType = L"video/mpegurl";
    hlsItem.upnpClass = L"object.item.videoItem";
    hlsItem.sizeBytes = 0;
    if (!IsRemoteMediaUrl(path)) {
        long long size = 0;
        if (FsFileSize(path, size)) hlsItem.sizeBytes = size;
    }
    hlsItem.title = !titleOverride.empty() ? titleOverride : SourceStemName(path);
    {
        std::time_t now = std::time(nullptr);
        struct tm utc;
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        char buf[11];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &utc);
        hlsItem.dcDate = buf;
        hlsItem.rawDateMs = static_cast<long long>(now) * 1000LL;
    }

    AppMedia.PublishItem(hlsItem);
    scanSuccess.Mark();
}

void MediaSources::ScanPlaylistTree(std::shared_ptr<PlaylistScanContext> ctx, const std::wstring& path,
                                     int parentId, const std::wstring& titleOverride) {
    {
        std::lock_guard<std::mutex> lock(ctx->queueMutex);
        ctx->pendingQueue.push_back({path, parentId, titleOverride, 0});
    }
    ctx->group.Enter();
    ctx->queueCv.notify_all();
    RunPlaylistDispatcher(ctx);
}

void MediaSources::RunPlaylistDispatcher(std::shared_ptr<PlaylistScanContext> ctx) {
    while (true) {
        PendingPlaylistNode node;
        {
            std::unique_lock<std::mutex> lock(ctx->queueMutex);
            ctx->queueCv.wait(lock, [&]() {
                return !ctx->pendingQueue.empty() || ctx->group.PendingCount() == 0;
            });
            if (ctx->pendingQueue.empty()) {
                if (ctx->group.PendingCount() == 0) break;
                continue;
            }
            node = std::move(ctx->pendingQueue.front());
            ctx->pendingQueue.pop_front();
        }
        ctx->limiter.Acquire();
        PlaylistScanPool::Get().Submit([this, ctx, node]() {
            // the leave guard must go out of scope and run group Leave
            // before queueCv is notified
            // Leave decrements the pending count the dispatcher predicate
            // reads and that decrement must be visible before the notify
            // fires or the dispatcher can observe a stale count and never
            // wake again once this is the task that brings count to zero
            // see the workflow doc section 1 3 for the full trace
            {
                TaskGroupLeaveGuard leave(ctx->group);
                ScanOnePlaylistNode(ctx, node, leave);
                ctx->limiter.Release();
            }
            ctx->queueCv.notify_all();
        });
    }
    ctx->group.Wait();
}

void MediaSources::ScanOnePlaylistNode(std::shared_ptr<PlaylistScanContext> ctx, const PendingPlaylistNode& node, TaskGroupLeaveGuard& guard) {
    (void)guard;
    if (node.depth > kMaxPlaylistRecursionDepth) {
        LogPrint(L"[media:scan-depth] Skipping playlist due to recursion depth limit: %ls",
                 RedactUrlForLog(node.path).c_str());
        return;
    }

    FetchedPlaylist fetched = FetchPlaylistOnce(node.path);

    if (!fetched.fetchOk) {
        LogPrint(L"[media:fetch-failed] Playlist could not be fetched; treating as unavailable rather than empty: %ls",
                 RedactUrlForLog(node.path).c_str());
        if (ctx->state.mediaDatabase) {
            ctx->state.mediaDatabase->RecordScanError(
                BuildStableContainerKey(node.parentId, SourceStemName(node.path), node.path, g_canonicalize),
                L"Playlist fetch failed");
        }
        return;
    }

    if (fetched.isHls) {
        LogPrint(L"Detected HLS manifest, exposing as a single stream: %ls", RedactUrlForLog(node.path).c_str());
        AppMedia.AddHlsStreamItem(ctx->state, node.path, node.parentId, node.titleOverride);
        return;
    }

    if (!IsRecognizedPlaylistText(node.path, fetched.text)) {
        LogPrint(L"[media:fetch-invalid] Fetched content is not a recognized HLS manifest or M3U/PLS playlist; skipping: %ls",
                 RedactUrlForLog(node.path).c_str());
        if (ctx->state.mediaDatabase) {
            ctx->state.mediaDatabase->RecordScanError(
                BuildStableContainerKey(node.parentId, SourceStemName(node.path), node.path, g_canonicalize),
                L"Playlist content not recognized");
        }
        return;
    }

    auto entries = ParseFetchedPlaylistText(node.path, fetched.text);
    if (entries.empty()) {
        LogPrint(L"Skipping playlist with no usable entries: %ls", RedactUrlForLog(node.path).c_str());
        return;
    }

    const int folderId = FindOrAddContainer(ctx->state, node.parentId,
        node.titleOverride.empty() ? SourceStemName(node.path) : node.titleOverride,
        node.path, g_canonicalize);

    std::vector<PendingPlaylistNode> newlyDiscovered;
    for (const auto& entry : entries) {
        if (IsPlaylistSourcePath(entry.location)) {
            ++ctx->discoveredCount;
            ctx->limiter.SetLimit(ComputePlaylistScanConcurrency(ctx->discoveredCount.load()));
            newlyDiscovered.push_back(PendingPlaylistNode{entry.location, folderId, entry.title, node.depth + 1});
            continue;
        }
        AppMedia.AddMediaFile(ctx->state, ctx->cfg, entry.location, folderId, entry.title, entry.subtitlePath);
    }

    if (!newlyDiscovered.empty()) {
        std::lock_guard<std::mutex> lock(ctx->queueMutex);
        for (auto& child : newlyDiscovered) {
            ctx->group.Enter();
            ctx->pendingQueue.push_back(std::move(child));
        }
    }
    ctx->queueCv.notify_all();
}

void MediaSources::ScanNetworkFolder(std::shared_ptr<PlaylistScanContext> sourceContext, const std::wstring& folderUrl, int parentId, int depth) {
    if (depth > 8) {
        LogPrint(L"%ls Skipping network folder due to recursion depth limit: %ls", kScanDepthLogCode, RedactUrlForLog(folderUrl).c_str());
        return;
    }

    for (const auto& entry : ListRemoteDirectory(folderUrl)) {
        if (IsPlaylistSourcePath(entry.url)) {
            ScanPlaylistTree(sourceContext, entry.url, parentId, SourceStemName(entry.name));
            continue;
        }

        std::wstring mime, uclass;
        if (IsAllowedExtension(SourceExtension(entry.url), mime, uclass)) {
            AddMediaFile(sourceContext->state, sourceContext->cfg, entry.url, parentId);
            continue;
        }

        if (entry.likelyDirectory) {
            if (sourceContext->cfg.flatFolderStyle) {
                ScanNetworkFolder(sourceContext, entry.url, parentId, depth + 1);
            } else {
                const int folderId = FindOrAddContainer(sourceContext->state, parentId, SourceDisplayName(entry.name), entry.url, g_canonicalize);
                ScanNetworkFolder(sourceContext, entry.url, folderId, depth + 1);
            }
        }
    }
}

void MediaSources::ScanFolder(std::shared_ptr<PlaylistScanContext> sourceContext, const std::wstring& rootPath, int parentId, int depth) {
    if (depth > 64) {
        LogPrint(L"%ls Skipping folder due to recursion depth limit: %ls", kScanDepthLogCode, rootPath.c_str());
        return;
    }

    std::vector<FsDirEntry> entries;
    if (!FsListDirectory(rootPath, entries)) {
        if (sourceContext->state.mediaDatabase) {
            sourceContext->state.mediaDatabase->RecordScanError(BuildStableContainerKey(parentId, SourceDisplayName(rootPath), rootPath, g_canonicalize), L"Folder unavailable");
        }
        LogPrint(L"Skipping unreadable folder: %ls", rootPath.c_str());
        return;
    }

    for (const auto& entry : entries) {
        if (entry.isDirectory) {
            if (sourceContext->cfg.flatFolderStyle) {
                ScanFolder(sourceContext, entry.fullPath, parentId, depth + 1);
            } else {
                const int folderId = FindOrAddContainer(sourceContext->state, parentId, entry.name, entry.fullPath, g_canonicalize);
                ScanFolder(sourceContext, entry.fullPath, folderId, depth + 1);
            }
        } else {
            if (IsPlaylistSourcePath(entry.fullPath)) {
                ScanPlaylistTree(sourceContext, entry.fullPath, parentId);
            } else {
                AddMediaFile(sourceContext->state, sourceContext->cfg, entry.fullPath, parentId);
            }
        }
    }
}

MediaSources::GetChildrenResult MediaSources::TryGetChildren(int objId, std::vector<MediaItem>& out) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto found = m_idToIndex.find(objId);
    if (found == m_idToIndex.end() || found->second >= m_items.size()) {
        out.clear();
        return GetChildrenResult::NotFound;
    }
    const MediaItem& item = m_items[found->second];
    if (!item.isFolder) {
        out.clear();
        return GetChildrenResult::NotAContainer;
    }
    out.clear();
    auto childrenFound = m_childrenByParent.find(objId);
    if (childrenFound != m_childrenByParent.end()) {
        for (size_t index : childrenFound->second) {
            if (index < m_items.size()) out.push_back(m_items[index]);
        }
    }
    if (AppConfig.IsSortByTitleEnabled()) {
        std::sort(out.begin(), out.end(), [](const MediaItem& a, const MediaItem& b) {
            if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
            return NaturalLessWide(a.title, b.title);
        });
    }
    return GetChildrenResult::Success;
}

std::vector<MediaItem> MediaSources::GetDescendants(int parentId) {
    std::vector<MediaItem> items;
    std::unordered_map<int, std::vector<size_t>> childrenByParent;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        items = m_items;
        childrenByParent = m_childrenByParent;
    }

    std::vector<MediaItem> result;
    AppendDescendants(items, childrenByParent, parentId, AppConfig.IsSortByTitleEnabled(), result);
    return result;
}

MediaItem MediaSources::GetItem(int id) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto found = m_idToIndex.find(id);
    if (found != m_idToIndex.end() && found->second < m_items.size()) {
        return m_items[found->second];
    }
    MediaItem m = {};
    m.id = -1;
    return m;
}

std::unordered_map<int, int> MediaSources::GetChildCounts(const std::vector<MediaItem>& items) {
    std::unordered_map<int, int> counts;
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& item : items) {
        if (!item.isFolder) continue;
        auto found = m_childrenByParent.find(item.id);
        counts[item.id] = found == m_childrenByParent.end() ? 0 : static_cast<int>(found->second.size());
    }
    return counts;
}

int MediaSources::GetSystemUpdateID() {
    return m_systemUpdateId.load(std::memory_order_acquire);
}

void MediaSources::ResetForRescan() {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_items.clear();
        m_idToIndex.clear();
        m_childrenByParent.clear();

        MediaItem root{};
        root.id = 0;
        root.parentId = -1;
        root.title = L"Root";
        root.isFolder = true;
        root.upnpClass = L"object.container.storageFolder";
        m_items.push_back(root);
        m_idToIndex[0] = 0;
    }
    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
}

int MediaSources::PublishContainer(MediaDatabase* database, int parentId,
                                    const std::wstring& title, const std::wstring& path,
                                    std::function<std::wstring(const std::wstring&)> canonicalize) {
    MediaItem container{};
    container.parentId = parentId;
    container.path = path;
    container.title = title;
    container.isFolder = true;
    container.upnpClass = L"object.container.storageFolder";
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        container.id = database
            ? database->GetOrCreateStableContainerId(
                  BuildStableContainerKey(parentId, title, path, canonicalize))
            : NextScratchId();
        m_items.push_back(container);
        const size_t index = m_items.size() - 1;
        m_idToIndex[container.id] = index;
        m_childrenByParent[parentId].push_back(index);
    }
    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
    return container.id;
}

// PublishItem is called once per scanned file and takes m_mutex (exclusive)
// plus, unconditionally, AppEvents.NotifySystemUpdateId()'s own mutex. Both
// are cheap, uncontended std::mutex/std::shared_mutex acquisitions at the
// concurrency level this project runs at (bounded by PlaylistScanPool's 20
// workers -- see playlist_scan_concurrency.h). NotifySystemUpdateId's own
// GENA-dispatch cost is already debounced to a 500ms window
// (upnp_eventing.h: m_minNotifyInterval). A lock-free fast path was
// evaluated here and intentionally not added: it would only save an
// uncontended mutex acquisition, which is not the actual cost driver, and
// it would add a second, easy-to-desync source of truth for "are we inside
// the debounce window" (see UpnpEventManager::m_lastDispatchTime). If
// profiling ever shows this path as hot at higher configured concurrency,
// revisit by widening PlaylistScanPool sizing and re-measuring before
// reaching for a lock-free rewrite here.
void MediaSources::PublishItem(MediaItem item) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_items.push_back(std::move(item));
        const size_t index = m_items.size() - 1;
        const MediaItem& stored = m_items.back();
        m_idToIndex[stored.id] = index;
        m_childrenByParent[stored.parentId].push_back(index);
    }
    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
}