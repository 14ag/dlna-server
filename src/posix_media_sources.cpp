#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_database.h"
#include "media_scan_common.h"
#include "netutils.h"
#include "network_sources.h"
#include "upnp_eventing.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace {
constexpr const wchar_t* kScanDepthLogCode = L"[media:scan-depth]";

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
    MediaDatabase database;
    database.Load(MediaDatabase::DefaultDatabasePath());
    MediaIndexState state;
    state.mediaDatabase = &database;
    state.items.push_back({0, -1, L"", L"Root", true, L"", L"object.container.storageFolder", 0, L"", L"", L""});
    for (const auto& source : cfg.mediaSources) {
        if (!source.enabled) continue;
        if (IsRemovedSmbSourcePath(source.path)) {
            LogPrint(L"[media:smb-removed] SMB (smb://, smbs://) media sources are no longer supported; skipping: %ls", RedactUrlForLog(source.path).c_str());
            continue;
        }
        if (!IsPlaylistSourcePath(source.path) && !IsNetworkShareUrl(source.path)) {
            fs::path path(WideToUtf8(source.path));
            if (!fs::exists(path)) {
                database.RecordScanError(CanonicalMediaKey(source.path), L"Source unavailable");
                LogPrint(L"Skipping missing source: %ls", source.path.c_str());
                continue;
            }
        }
        LogPrint(L"Scanning media source: %ls", RedactUrlForLog(source.path).c_str());
        const int itemsBefore = static_cast<int>(state.items.size());
        MediaItem folder{};
            folder.id = AllocateContainerId(state, 0, SourceDisplayName(source.path), source.path, g_canonicalize);
        folder.parentId = 0;
        folder.path = source.path;
        folder.title = SourceDisplayName(source.path);
        folder.isFolder = true;
        folder.upnpClass = L"object.container.storageFolder";
        state.items.push_back(folder);

        if (IsPlaylistSourcePath(source.path)) {
            ScanPlaylist(state, cfg, source.path, folder.id);
        } else if (IsNetworkShareUrl(source.path)) {
            ScanNetworkFolder(state, cfg, source.path, folder.id, 0);
        } else {
            ScanFolder(state, cfg, source.path, folder.id);
        }
        const int sourceItemCount = static_cast<int>(state.items.size()) - itemsBefore;
        LogPrint(L"Scanned %d media items from %ls", sourceItemCount, SourceDisplayName(source.path).c_str());
    }
    if (cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty()) {
        fs::path path(WideToUtf8(cfg.defaultPlaylistPath));
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            const int itemsBefore = static_cast<int>(state.items.size());
            MediaItem playlistFolder{};
            playlistFolder.id = AllocateContainerId(state, 0, L"Default playlist", cfg.defaultPlaylistPath, g_canonicalize);
            playlistFolder.parentId = 0;
            playlistFolder.path = cfg.defaultPlaylistPath;
            playlistFolder.title = L"Default playlist";
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            state.items.push_back(playlistFolder);
            ScanPlaylist(state, cfg, cfg.defaultPlaylistPath, playlistFolder.id);
            const int sourceItemCount = static_cast<int>(state.items.size()) - itemsBefore;
            LogPrint(L"Scanned %d media items from Default playlist", sourceItemCount);
        }
    }
    int mediaItemCount = static_cast<int>(std::count_if(state.items.begin(), state.items.end(), [](const MediaItem& item) {
        return !item.isFolder;
    }));
    BuildIndexes(state);
    SwapScannedState(std::move(state));
    const int newUpdateId = m_systemUpdateId.fetch_add(1, std::memory_order_acq_rel) + 1;
    AppEvents.NotifySystemUpdateId(newUpdateId);
    if (!database.Save(MediaDatabase::DefaultDatabasePath())) {
        LogPrint(L"Media database save failed: %ls", MediaDatabase::DefaultDatabasePath().c_str());
    }
    LogPrint(L"Scanned %d media items.", mediaItemCount);
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

    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, pathText, g_canonicalize)).second) return;

    MediaItem file{};
    const std::wstring stableKey = BuildStableMediaKey(parentId, pathText, g_canonicalize);
    ScopedScanSuccess scanSuccess(state.mediaDatabase, stableKey);
    file.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : state.nextId++;
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
    state.items.push_back(file);
    scanSuccess.Mark();
    if (allowArtistAlbumMirror) {
        AddArtistAlbumMirrorIfPresent(state, cfg, file, parentId, posixParentPathOf, posixNameOfPath, g_canonicalize);
    }
}

void MediaSources::AddHlsStreamItem(MediaIndexState& state, const std::wstring& path, int parentId, const std::wstring& titleOverride) {
    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, path, g_canonicalize)).second) return;

    MediaItem hlsItem{};
    const std::wstring stableKey = BuildStableMediaKey(parentId, path, g_canonicalize);
    ScopedScanSuccess scanSuccess(state.mediaDatabase, stableKey);
    hlsItem.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : state.nextId++;
    hlsItem.parentId = parentId;
    hlsItem.path = path;
    hlsItem.isFolder = false;
    hlsItem.mimeType = L"application/vnd.apple.mpegurl";
    hlsItem.upnpClass = L"object.item.videoItem";
    hlsItem.sizeBytes = 0;
    hlsItem.title = !titleOverride.empty() ? titleOverride : SourceStemName(path);

    state.items.push_back(hlsItem);
    scanSuccess.Mark();
}

void MediaSources::ScanPlaylist(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& playlistPath, int parentId, int depth) {
    if (depth > 8) {
        LogPrint(L"%ls Skipping playlist due to recursion depth limit: %ls", kScanDepthLogCode, RedactUrlForLog(playlistPath).c_str());
        return;
    }

    FetchedPlaylist fetched = FetchPlaylistOnce(playlistPath);
    if (!fetched.fetchOk) {
        LogPrint(L"[media:fetch-failed] Playlist could not be fetched; treating as unavailable rather than empty: %ls", RedactUrlForLog(playlistPath).c_str());
        if (state.mediaDatabase) {
            state.mediaDatabase->RecordScanError(BuildStableContainerKey(parentId, SourceStemName(playlistPath), playlistPath, g_canonicalize), L"Playlist fetch failed");
        }
        return;
    }

    if (fetched.isHls) {
        // RFC 8216: this file is a Master or Media Playlist (contains at least
        // one #EXT-X- tag) Its Media Segments exist purely so an HLS-aware
        // player can fetch them itself they must NOT be enumerated as separate
        // DLNA items or a UPnP renderer will play each segment as its own
        // track with a stall at every boundary Expose the manifest itself as
        // one playable resource
        LogPrint(L"Detected HLS manifest, exposing as a single stream: %ls", RedactUrlForLog(playlistPath).c_str());
        AddHlsStreamItem(state, playlistPath, parentId);
        return;
    }

    auto entries = ParseFetchedPlaylistText(playlistPath, fetched.text);
    LogPrint(L"Loaded %d entries from playlist: %ls", static_cast<int>(entries.size()), RedactUrlForLog(playlistPath).c_str());
    for (const auto& entry : entries) {
        if (IsPlaylistSourcePath(entry.location)) {
            ScanPlaylistEntry(state, cfg, entry.location, entry.title, parentId, depth + 1);
            continue;
        }
        AddMediaFile(state, cfg, entry.location, parentId, entry.title, entry.subtitlePath);
    }
}

void MediaSources::ScanPlaylistEntry(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& location, const std::wstring& titleOverride, int parentId, int depth) {
    if (depth > 8) {
        LogPrint(L"%ls Skipping playlist due to recursion depth limit: %ls", kScanDepthLogCode, RedactUrlForLog(location).c_str());
        return;
    }

    FetchedPlaylist fetched = FetchPlaylistOnce(location);
    if (!fetched.fetchOk) {
        LogPrint(L"[media:fetch-failed] Playlist could not be fetched; treating as unavailable rather than empty: %ls", RedactUrlForLog(location).c_str());
        if (state.mediaDatabase) {
            state.mediaDatabase->RecordScanError(BuildStableContainerKey(parentId, SourceStemName(location), location, g_canonicalize), L"Playlist fetch failed");
        }
        return;
    }

    if (fetched.isHls) {
        // The referenced .m3u8/.m3u/.pls is itself an HLS manifest
        // Do not wrap it in its own container a container whose only
        // possible child is one manifest item with no size or duration
        // adds a browse hop with nothing distinguishable inside it
        // Register the manifest directly under the CURRENT container
        LogPrint(L"Detected HLS manifest, exposing as a single stream: %ls", RedactUrlForLog(location).c_str());
        AddHlsStreamItem(state, location, parentId, titleOverride);
        return;
    }

    auto entries = ParseFetchedPlaylistText(location, fetched.text);
    if (entries.empty()) {
        // nothing to show do not advertise an empty folder to clients
        LogPrint(L"Skipping nested playlist with no usable entries: %ls", RedactUrlForLog(location).c_str());
        return;
    }

    MediaItem playlistFolder;
    playlistFolder.id = AllocateContainerId(state, parentId, SourceStemName(location), location, g_canonicalize);
    playlistFolder.parentId = parentId;
    playlistFolder.path = location;
    playlistFolder.title = titleOverride.empty() ? SourceStemName(location) : titleOverride;
    playlistFolder.isFolder = true;
    playlistFolder.upnpClass = L"object.container.storageFolder";
    state.items.push_back(playlistFolder);

    for (const auto& entry : entries) {
        if (IsPlaylistSourcePath(entry.location)) {
            ScanPlaylistEntry(state, cfg, entry.location, entry.title, playlistFolder.id, depth + 1);
            continue;
        }
        AddMediaFile(state, cfg, entry.location, playlistFolder.id, entry.title, entry.subtitlePath);
    }
}

void MediaSources::ScanNetworkFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& folderUrl, int parentId, int depth) {
    if (depth > 8) {
        LogPrint(L"%ls Skipping network folder due to recursion depth limit: %ls", kScanDepthLogCode, RedactUrlForLog(folderUrl).c_str());
        return;
    }

    for (const auto& entry : ListRemoteDirectory(folderUrl)) {
        if (IsPlaylistSourcePath(entry.url)) {
            MediaItem playlistFolder{};
            playlistFolder.id = AllocateContainerId(state, parentId, SourceStemName(entry.name), entry.url, g_canonicalize);
            playlistFolder.parentId = parentId;
            playlistFolder.path = entry.url;
            playlistFolder.title = SourceStemName(entry.name);
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            state.items.push_back(playlistFolder);
            ScanPlaylist(state, cfg, entry.url, playlistFolder.id);
            continue;
        }

        std::wstring mime, uclass;
        if (IsAllowedExtension(SourceExtension(entry.url), mime, uclass)) {
            AddMediaFile(state, cfg, entry.url, parentId);
            continue;
        }

        if (entry.likelyDirectory) {
            if (cfg.flatFolderStyle) {
                ScanNetworkFolder(state, cfg, entry.url, parentId, depth + 1);
            } else {
                MediaItem folder{};
                folder.id = AllocateContainerId(state, parentId, SourceDisplayName(entry.name), entry.url, g_canonicalize);
                folder.parentId = parentId;
                folder.path = entry.url;
                folder.title = SourceDisplayName(entry.name);
                folder.isFolder = true;
                folder.upnpClass = L"object.container.storageFolder";
                state.items.push_back(folder);
                ScanNetworkFolder(state, cfg, entry.url, folder.id, depth + 1);
            }
        }
    }
}

void MediaSources::ScanFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& rootPath, int parentId, int depth) {
    if (depth > 64) {
        LogPrint(L"%ls Skipping folder due to recursion depth limit: %ls", kScanDepthLogCode, rootPath.c_str());
        return;
    }
    fs::path root(WideToUtf8(rootPath));
    std::error_code ec;
    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    if (ec) {
        if (state.mediaDatabase) {
            state.mediaDatabase->RecordScanError(BuildStableContainerKey(parentId, SourceDisplayName(rootPath), rootPath, g_canonicalize), L"Folder unavailable");
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
            if (cfg.flatFolderStyle) {
                ScanFolder(state, cfg, Utf8ToWide(path.u8string()), parentId, depth + 1);
            } else {
                MediaItem folder{};
                folder.id = AllocateContainerId(state, parentId, Utf8ToWide(path.filename().u8string()), Utf8ToWide(path.u8string()), g_canonicalize);
                folder.parentId = parentId;
                folder.path = Utf8ToWide(path.u8string());
                folder.title = Utf8ToWide(path.filename().u8string());
                folder.isFolder = true;
                folder.upnpClass = L"object.container.storageFolder";
                state.items.push_back(folder);
                ScanFolder(state, cfg, folder.path, folder.id, depth + 1);
            }
        } else if (!skip && entry.is_regular_file(ec)) {
            std::wstring fullPath = Utf8ToWide(path.u8string());
            if (IsPlaylistSourcePath(fullPath)) {
                MediaItem playlistFolder{};
                playlistFolder.id = AllocateContainerId(state, parentId, SourceStemName(fullPath), fullPath, g_canonicalize);
                playlistFolder.parentId = parentId;
                playlistFolder.path = fullPath;
                playlistFolder.title = SourceStemName(fullPath);
                playlistFolder.isFolder = true;
                playlistFolder.upnpClass = L"object.container.storageFolder";
                state.items.push_back(playlistFolder);
                ScanPlaylist(state, cfg, fullPath, playlistFolder.id);
            } else {
                AddMediaFile(state, cfg, fullPath, parentId);
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
    const bool sortByTitle = AppConfig.Snapshot().sortByTitle;
    if (sortByTitle) {
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
    const bool sortByTitle = AppConfig.Snapshot().sortByTitle;
    if (sortByTitle) {
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
    AppendDescendants(items, childrenByParent, parentId, AppConfig.Snapshot().sortByTitle, result);
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

void MediaSources::BuildIndexes(MediaIndexState& state) {
    state.idToIndex.clear();
    state.childrenByParent.clear();
    for (size_t i = 0; i < state.items.size(); ++i) {
        state.idToIndex[state.items[i].id] = i;
        state.childrenByParent[state.items[i].parentId].push_back(i);
    }
}

void MediaSources::SwapScannedState(MediaIndexState&& state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items = std::move(state.items);
    m_idToIndex = std::move(state.idToIndex);
    m_childrenByParent = std::move(state.childrenByParent);
}
