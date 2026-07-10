#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_database.h"
#include "media_scan_common.h"
#include "netutils.h"
#include "network_sources.h"
#include "playlist_scan_concurrency.h"
#include "task_group.h"
#include "upnp_eventing.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <functional>
#include <utility>

namespace fs = std::filesystem;

namespace {
constexpr const wchar_t* kScanDepthLogCode = L"[media:scan-depth]";

bool DefaultPlaylistFileExists(const std::wstring& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(WideToUtf8(path), ec);
}

bool IsHiddenPath(const fs::path& path) {
    const std::string name = path.filename().u8string();
    return !name.empty() && name[0] == '.';
}

bool HasAnyReadBit(fs::perms permissions) {
    using fs::perms;
    return (permissions & (perms::owner_read | perms::group_read | perms::others_read)) != perms::none;
}

bool IsReadableEntry(const fs::directory_entry& entry) {
    std::error_code ec;
    const fs::file_status status = entry.status(ec);
    return !ec && HasAnyReadBit(status.permissions());
}

std::wstring CanonicalMediaKey(const std::wstring& pathText) {
    if (IsRemoteMediaUrl(pathText)) {
        return ToLowerWide(pathText);
    }
    std::error_code ec;
    fs::path path(WideToUtf8(pathText));
    fs::path canonical = fs::weakly_canonical(path, ec);
    return Utf8ToWide((ec ? path : canonical).u8string());
}

const auto g_canonicalize = [](const std::wstring& path) -> std::wstring {
    return CanonicalMediaKey(path);
};

const auto posixParentPathOf = [](const std::wstring& p) -> std::wstring {
    fs::path path(WideToUtf8(p));
    return Utf8ToWide(path.parent_path().u8string());
};

const auto posixNameOfPath = [](const std::wstring& p) -> std::wstring {
    fs::path path(WideToUtf8(p));
    return Utf8ToWide(path.filename().u8string());
};

void SetAlbumArtIfExists(MediaIndexState& state, MediaItem& item) {
    if (IsRemoteMediaUrl(item.path)) return;
    std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
    fs::path path(WideToUtf8(item.path));
    const std::wstring directoryKey = Utf8ToWide(path.parent_path().u8string());
    const std::wstring stem = Utf8ToWide(path.stem().u8string());
    const std::wstring stemKey = directoryKey + L"\n" + stem;

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
        fs::path candidatePath = path.parent_path() / WideToUtf8(candidate.fileName);
        std::error_code ec;
        if (fs::is_regular_file(candidatePath, ec)) {
            item.albumArtPath = Utf8ToWide(candidatePath.u8string());
            item.albumArtMime = candidate.mimeType;
            state.perStemAlbumArt[stemKey] = { item.albumArtPath, item.albumArtMime };
            return;
        }
    }
    state.perStemAlbumArt[stemKey] = { L"", L"" };

    auto folderCached = state.folderAlbumArt.find(directoryKey);
    if (folderCached != state.folderAlbumArt.end()) {
        item.albumArtPath = folderCached->second.first;
        item.albumArtMime = folderCached->second.second;
        return;
    }
    for (const auto& candidate : BuildAlbumArtCandidateNames(L"")) {
        fs::path candidatePath = path.parent_path() / WideToUtf8(candidate.fileName);
        std::error_code ec;
        if (fs::is_regular_file(candidatePath, ec)) {
            item.albumArtPath = Utf8ToWide(candidatePath.u8string());
            item.albumArtMime = candidate.mimeType;
            state.folderAlbumArt[directoryKey] = { item.albumArtPath, item.albumArtMime };
            state.albumArtByDirectory[directoryKey] = { item.albumArtPath, item.albumArtMime };
            return;
        }
    }
    state.folderAlbumArt[directoryKey] = { L"", L"" };
    state.albumArtByDirectory[directoryKey] = { L"", L"" };
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

    ResetForRescan();

    struct SourceJob {
        std::shared_ptr<PlaylistScanContext> ctx;
        MediaSource source;
        int containerId;
    };
    std::vector<SourceJob> jobs;

    for (const auto& src : cfg.mediaSources) {
        if (!src.enabled) continue;
        if (IsRemovedSmbSourcePath(src.path)) {
            LogPrint(L"[media:smb-removed] SMB media sources are no longer supported; skipping: %ls",
                     RedactUrlForLog(src.path).c_str());
            continue;
        }
        if (!IsPlaylistSourcePath(src.path) && !IsNetworkShareUrl(src.path)) {
            fs::path path(WideToUtf8(src.path));
            if (!fs::exists(path)) {
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

    if (cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty() && DefaultPlaylistFileExists(cfg.defaultPlaylistPath)) {
        const int containerId = PublishContainer(database.get(), 0, L"Default playlist", cfg.defaultPlaylistPath, g_canonicalize);
        auto ctx = std::make_shared<PlaylistScanContext>();
        ctx->cfg = cfg;
        ctx->state.mediaDatabase = database.get();
        jobs.push_back({ctx, MediaSource{cfg.defaultPlaylistPath, true}, containerId});
    }

    std::vector<std::thread> sourceThreads;
    for (auto& job : jobs) {
        sourceThreads.emplace_back([this, &job]() {
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
    for (auto& t : sourceThreads) t.join();

    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
    if (!database->Save(MediaDatabase::DefaultDatabasePath())) {
        LogPrint(L"Media database save failed: %ls", MediaDatabase::DefaultDatabasePath().c_str());
    }
    LogPrint(L"Scan complete.");
}

void MediaSources::AddMediaFile(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& pathText, int parentId, const std::wstring& titleOverride, const std::wstring& subtitleOverride, bool allowArtistAlbumMirror) {
    std::wstring mime, uclass;
    std::wstring ext = SourceExtension(pathText);
    if (!IsAllowedExtension(ext, mime, uclass)) {
        if (IsRemoteMediaUrl(pathText) && ext.empty()) {
            mime = L"audio/mpeg";
            uclass = L"object.item.audioItem.musicTrack";
        } else {
            LogPrint(L"[media:reject-extension] Skipping media with unsupported extension '%ls': %ls", ext.c_str(), RedactUrlForLog(pathText).c_str());
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
        if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, pathText, g_canonicalize)).second) return;
    }

    MediaItem file{};
    const std::wstring stableKey = BuildStableMediaKey(parentId, pathText, g_canonicalize);
    ScopedScanSuccess scanSuccess(state.mediaDatabase, stableKey);
    static std::atomic<int> s_scratchMediaId{-1000002};
    file.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : s_scratchMediaId.fetch_sub(1, std::memory_order_relaxed);
    file.parentId = parentId;
    file.path = pathText;
    file.isFolder = false;
    file.mimeType = mime;
    file.upnpClass = uclass;

    if (IsRemoteMediaUrl(pathText)) {
        file.sizeBytes = ProbeRemoteContentLength(pathText);
    } else {
        fs::path path(WideToUtf8(pathText));
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) return;
        const uintmax_t fileSize = fs::file_size(path, ec);
        if (ec) {
            LogPrint(L"[media:file-size] Skipping file with unreadable size: %ls", pathText.c_str());
            return;
        }
        file.sizeBytes = static_cast<long long>(fileSize);
    }

    if (cfg.showFileNamesInsteadOfTitles) file.title = SourceDisplayName(pathText);
    else if (!titleOverride.empty()) file.title = titleOverride;
    else file.title = SourceStemName(pathText);

    if (!subtitleOverride.empty()) {
        file.subtitlePath = subtitleOverride;
        if (IsRemoteMediaUrl(subtitleOverride)) {
            LogPrint(L"Playlist subtitle resolved to remote URL: %ls", RedactUrlForLog(subtitleOverride).c_str());
        }
    } else if (!IsRemoteMediaUrl(pathText) && uclass == L"object.item.videoItem") {
        fs::path path(WideToUtf8(pathText));
        static const char* kSubExts[] = { ".srt", ".vtt", ".sub", ".ass", ".ssa", ".smi", ".txt" };
        for (const char* subExt : kSubExts) {
            fs::path candidate = path.parent_path() / (path.stem().u8string() + subExt);
            std::error_code subEc;
            if (fs::is_regular_file(candidate, subEc)) {
                file.subtitlePath = Utf8ToWide(candidate.u8string());
                break;
            }
        }
    }

    SetAlbumArtIfExists(state, file);
    AppMedia.PublishItem(file);
    scanSuccess.Mark();
    if (allowArtistAlbumMirror) {
        AddArtistAlbumMirrorIfPresent(state, cfg, file, parentId, posixParentPathOf, posixNameOfPath, g_canonicalize);
    }
}

void MediaSources::AddHlsStreamItem(MediaIndexState& state, const std::wstring& path, int parentId, const std::wstring& titleOverride) {
    {
        std::lock_guard<std::mutex> lock(*state.mutationMutex.get());
        if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, path, g_canonicalize)).second) return;
    }

    MediaItem hlsItem{};
    const std::wstring stableKey = BuildStableMediaKey(parentId, path, g_canonicalize);
    ScopedScanSuccess scanSuccess(state.mediaDatabase, stableKey);
    static std::atomic<int> s_scratchHlsId{-2000002};
    hlsItem.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : s_scratchHlsId.fetch_sub(1, std::memory_order_relaxed);
    hlsItem.parentId = parentId;
    hlsItem.path = path;
    hlsItem.isFolder = false;
    hlsItem.mimeType = L"video/mpegurl";
    hlsItem.upnpClass = L"object.item.videoItem";
    hlsItem.sizeBytes = 0;
    hlsItem.title = !titleOverride.empty() ? titleOverride : SourceStemName(path);
    // stamp scan date matching Android rawDate/dc:date fields
    {
        std::time_t now = std::time(nullptr);
        struct tm utc;
        gmtime_r(&now, &utc);
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
                continue; // spurious wake / another dispatcher thread took the last item
            }
            node = std::move(ctx->pendingQueue.front());
            ctx->pendingQueue.pop_front();
        }
        ctx->limiter.Acquire(); // safe to block: this is the dispatcher thread, never a pool worker
        PlaylistScanPool::Get().Submit([this, ctx, node]() {
            TaskGroupLeaveGuard leave(ctx->group);
            ScanOnePlaylistNode(ctx, node, leave);
            ctx->limiter.Release();
            ctx->queueCv.notify_all(); // wake the dispatcher in case it is waiting on PendingCount()
        });
    }
    ctx->group.Wait();
}

void MediaSources::ScanOnePlaylistNode(std::shared_ptr<PlaylistScanContext> ctx, const PendingPlaylistNode& node, TaskGroupLeaveGuard& guard) {
    (void)guard; // unused but needed for signature
    if (node.depth > kMaxPlaylistRecursionDepth) {
        LogPrint(L"[media:scan-depth] Skipping playlist due to recursion depth limit: %ls",
                 RedactUrlForLog(node.path).c_str());
        return;
    }

    // NETWORK I/O -- no lock of any kind is held here. This is the single
    // most important invariant in this file: holding state.mutationMutex or
    // MediaSources::m_mutex across this call re-serializes every scan task
    // that touches the same source or the same live index and silently
    // reproduces the original bug while looking "concurrent" on paper.
    FetchedPlaylist fetched = FetchPlaylistOnce(node.path);

    if (!fetched.fetchOk) {
        LogPrint(L"[media:fetch-failed] Playlist could not be fetched; treating as unavailable rather than empty: %ls",
                 RedactUrlForLog(node.path).c_str());
        if (ctx->state.mediaDatabase) {
            ctx->state.mediaDatabase->RecordScanError(
                BuildStableContainerKey(node.parentId, SourceStemName(node.path), node.path, g_canonicalize),
                L"Playlist fetch failed");
        }
        return; // only this node is dropped; the parent container published
                 // for it earlier (or its own parent's container) is
                 // unaffected -- this is what fixes "parent playlist never
                 // included": the container was already live before this
                 // fetch ever started.
    }

    if (fetched.isHls) {
        LogPrint(L"Detected HLS manifest, exposing as a single stream: %ls", RedactUrlForLog(node.path).c_str());
        AppMedia.AddHlsStreamItem(ctx->state, node.path, node.parentId, node.titleOverride);
        return;
    }

    auto entries = ParseFetchedPlaylistText(node.path, fetched.text);
    if (entries.empty()) {
        LogPrint(L"Skipping playlist with no usable entries: %ls", RedactUrlForLog(node.path).c_str());
        return;
    }

    // Publish this node's own container immediately -- this is the
    // "container created, then content added procedurally" requirement.
    // Every DIDL browse of the parent from this point on will show this
    // folder, even though its children are still being fetched.
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
        // Plain media file entries are cheap relative to playlist fetches
        // (ProbeRemoteContentLength has its own existing global limiter,
        // GetRemoteProbeLimiter() in network_sources.cpp, unrelated to this
        // task) -- publish them directly, synchronously, on this pool
        // worker.
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
    fs::path root(WideToUtf8(rootPath));
    std::error_code ec;
    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    if (ec) {
        if (sourceContext->state.mediaDatabase) {
            sourceContext->state.mediaDatabase->RecordScanError(BuildStableContainerKey(parentId, SourceDisplayName(rootPath), rootPath, g_canonicalize), L"Folder unavailable");
        }
        LogPrint(L"Skipping unreadable folder: %ls", rootPath.c_str());
        return;
    }
    while (it != end) {
        const fs::directory_entry entry = *it;
        ec.clear();
        const fs::path path = entry.path();
        bool skip = entry.is_symlink(ec) || IsHiddenPath(path) || !IsReadableEntry(entry);
        if (!skip && entry.is_directory(ec)) {
            if (sourceContext->cfg.flatFolderStyle) {
                ScanFolder(sourceContext, Utf8ToWide(path.u8string()), parentId, depth + 1);
            } else {
                const std::wstring fullPath = Utf8ToWide(path.u8string());
                const int folderId = FindOrAddContainer(sourceContext->state, parentId, Utf8ToWide(path.filename().u8string()), fullPath, g_canonicalize);
                ScanFolder(sourceContext, fullPath, folderId, depth + 1);
            }
        } else if (!skip && entry.is_regular_file(ec)) {
            std::wstring fullPath = Utf8ToWide(path.u8string());
            if (IsPlaylistSourcePath(fullPath)) {
                ScanPlaylistTree(sourceContext, fullPath, parentId);
            } else {
                AddMediaFile(sourceContext->state, sourceContext->cfg, fullPath, parentId);
            }
        }
        it.increment(ec);
        if (ec) {
            LogPrint(L"Skipping unreadable directory entry under: %ls", rootPath.c_str());
            ec.clear();
        }
    }
}

std::vector<MediaItem> MediaSources::GetChildren(int parentId) {
    std::vector<MediaItem> result;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto found = m_childrenByParent.find(parentId);
        if (found != m_childrenByParent.end()) {
            for (size_t index : found->second) {
                if (index < m_items.size()) result.push_back(m_items[index]);
            }
        }
    }
    if (AppConfig.IsSortByTitleEnabled()) {
        std::sort(result.begin(), result.end(), [](const MediaItem& a, const MediaItem& b) {
            if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
            return NaturalLessWide(a.title, b.title);
        });
    }
    return result;
}

MediaSources::GetChildrenResult MediaSources::TryGetChildren(int objId, std::vector<MediaItem>& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
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
        std::lock_guard<std::mutex> lock(m_mutex);
        items = m_items;
        childrenByParent = m_childrenByParent;
    }

    std::vector<MediaItem> result;
    AppendDescendants(items, childrenByParent, parentId, AppConfig.IsSortByTitleEnabled(), result);
    return result;
}

MediaItem MediaSources::GetItem(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_idToIndex.find(id);
    if (found != m_idToIndex.end() && found->second < m_items.size()) {
        return m_items[found->second];
    }
    MediaItem missing{};
    missing.id = -1;
    return missing;
}

std::unordered_map<int, int> MediaSources::GetChildCounts(const std::vector<MediaItem>& items) {
    std::unordered_map<int, int> counts;
    std::lock_guard<std::mutex> lock(m_mutex);
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
        std::lock_guard<std::mutex> lock(m_mutex);
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
        std::lock_guard<std::mutex> lock(m_mutex);
        static std::atomic<int> s_scratchId{-2};
        container.id = database
            ? database->GetOrCreateStableContainerId(
                  BuildStableContainerKey(parentId, title, path, canonicalize))
            : s_scratchId.fetch_sub(1, std::memory_order_relaxed);
        m_items.push_back(container);
        const size_t index = m_items.size() - 1;
        m_idToIndex[container.id] = index;
        m_childrenByParent[parentId].push_back(index);
    }
    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
    return container.id;
}

void MediaSources::PublishItem(MediaItem item) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.push_back(std::move(item));
        const size_t index = m_items.size() - 1;
        const MediaItem& stored = m_items.back();
        m_idToIndex[stored.id] = index;
        m_childrenByParent[stored.parentId].push_back(index);
    }
    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
}
