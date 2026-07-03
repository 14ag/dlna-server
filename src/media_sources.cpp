#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_database.h"
#include "network_sources.h"
#include "upnp_eventing.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <utility>
#include <vector>

MediaSources& MediaSources::Get() {
    static MediaSources instance;
    return instance;
}

MediaSources::MediaSources() : m_systemUpdateId(1) {
}

namespace {
int AllocateContainerId(MediaIndexState& state, int parentId, const std::wstring& title, const std::wstring& keyPath);
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

    // Root (0)
    MediaItem rootInfo;
    rootInfo.id = 0;
    rootInfo.parentId = -1;
    rootInfo.title = L"Root";
    rootInfo.isFolder = true;
    state.items.push_back(rootInfo);

    for (const auto& src : cfg.mediaSources) {
        if (!src.enabled) continue;

        MediaItem folderInfo;
        folderInfo.id = AllocateContainerId(state, 0, SourceDisplayName(src.path), src.path);
        folderInfo.parentId = 0;
        folderInfo.path = src.path;
        folderInfo.title = SourceDisplayName(src.path);
        folderInfo.isFolder = true;
        folderInfo.upnpClass = L"object.container.storageFolder";
        state.items.push_back(folderInfo);

        if (IsPlaylistSourcePath(src.path)) {
            ScanPlaylist(state, cfg, src.path, folderInfo.id);
        } else if (IsNetworkShareUrl(src.path)) {
            ScanNetworkFolder(state, cfg, src.path, folderInfo.id, 0);
        } else {
            ScanFolder(state, cfg, src.path, folderInfo.id);
        }
    }
    if (cfg.defaultPlaylistEnabled && !cfg.defaultPlaylistPath.empty()) {
        DWORD attrs = GetFileAttributesW(cfg.defaultPlaylistPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            MediaItem playlistFolder;
            playlistFolder.id = AllocateContainerId(state, 0, L"Default playlist", cfg.defaultPlaylistPath);
            playlistFolder.parentId = 0;
            playlistFolder.path = cfg.defaultPlaylistPath;
            playlistFolder.title = L"Default playlist";
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            state.items.push_back(playlistFolder);
            ScanPlaylist(state, cfg, cfg.defaultPlaylistPath, playlistFolder.id);
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

namespace {
constexpr const wchar_t* kScanDepthLogCode = L"[media:scan-depth]";

std::wstring CanonicalMediaKey(const std::wstring& path) {
    if (IsRemoteMediaUrl(path)) return ToLowerWide(path);
    DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (required > 0) {
        std::vector<wchar_t> full(required + 1);
        DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (written > 0 && written < full.size()) {
            return ToLowerWide(full.data());
        }
    }
    return ToLowerWide(path);
}

std::wstring BuildDuplicateMediaKey(int parentId, const std::wstring& path) {
    return L"media\n" + std::to_wstring(parentId) + L"\n" + CanonicalMediaKey(path);
}

std::wstring BuildStableMediaKey(int parentId, const std::wstring& path) {
    return BuildDuplicateMediaKey(parentId, path);
}

std::wstring ContainerLookupKey(int parentId, const std::wstring& title, const std::wstring& keyPath) {
    return std::to_wstring(parentId) + L"\n" + title + L"\n" + keyPath;
}

std::wstring BuildStableContainerKey(int parentId, const std::wstring& title, const std::wstring& keyPath) {
    return L"container\n" + ContainerLookupKey(parentId, title, CanonicalMediaKey(keyPath));
}

int AllocateContainerId(MediaIndexState& state, int parentId, const std::wstring& title, const std::wstring& keyPath) {
    if (state.mediaDatabase) {
        return state.mediaDatabase->GetOrCreateStableContainerId(BuildStableContainerKey(parentId, title, keyPath));
    }
    return state.nextId++;
}

struct ScopedScanSuccess {
    ScopedScanSuccess(MediaDatabase* database, std::wstring key) : database(database), key(std::move(key)), marked(false) {}
    void Mark() {
        if (database && !marked) {
            database->MarkScanSuccess(key);
            marked = true;
        }
    }
    MediaDatabase* database;
    std::wstring key;
    bool marked;
};

void SetAlbumArtIfExists(MediaIndexState& state, MediaItem& item) {
    if (IsRemoteMediaUrl(item.path)) return;
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
        DWORD attrs = GetFileAttributesW(candidatePath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
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
        DWORD attrs = GetFileAttributesW(candidatePath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            item.albumArtPath = candidatePath;
            item.albumArtMime = candidate.mimeType;
            state.folderAlbumArt[folder] = { item.albumArtPath, item.albumArtMime };
            state.albumArtByDirectory[folder] = { item.albumArtPath, item.albumArtMime };
            return;
        }
    }
    state.folderAlbumArt[folder] = { L"", L"" };
    state.albumArtByDirectory[folder] = { L"", L"" };
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

int FindOrAddContainer(MediaIndexState& state, int parentId, const std::wstring& title, const std::wstring& keyPath) {
    const std::wstring lookupKey = ContainerLookupKey(parentId, title, keyPath);
    auto found = state.containerKeys.find(lookupKey);
    if (found != state.containerKeys.end()) {
        return found->second;
    }

    MediaItem folderInfo;
    folderInfo.id = AllocateContainerId(state, parentId, title, keyPath);
    folderInfo.parentId = parentId;
    folderInfo.path = keyPath;
    folderInfo.title = title;
    folderInfo.isFolder = true;
    folderInfo.upnpClass = L"object.container.storageFolder";
    state.items.push_back(folderInfo);
    state.containerKeys[lookupKey] = folderInfo.id;
    return folderInfo.id;
}

void AddArtistAlbumMirrorIfPresent(MediaIndexState& state, const ConfigSnapshot& cfg, const MediaItem& item, int sourceParentId) {
    if (!cfg.addArtistAlbumFolders || IsRemoteMediaUrl(item.path)) return;
    if (item.upnpClass != L"object.item.audioItem.musicTrack" && item.upnpClass != L"object.item.videoItem") return;

    const std::wstring albumPath = ParentPathOf(item.path);
    const std::wstring artistPath = ParentPathOf(albumPath);
    const std::wstring album = NameOfPath(albumPath);
    const std::wstring artist = NameOfPath(artistPath);
    if (artist.empty() || album.empty()) return;

    int artistId = FindOrAddContainer(state, sourceParentId, artist, artistPath);
    int albumId = FindOrAddContainer(state, artistId, album, albumPath);
    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(albumId, item.path)).second) return;

    MediaItem mirror = item;
    mirror.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(BuildStableMediaKey(albumId, item.path))
        : state.nextId++;
    mirror.parentId = albumId;
    state.items.push_back(mirror);
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

    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, path)).second) return;

    MediaItem fileInfo;
    const std::wstring stableKey = BuildStableMediaKey(parentId, path);
    ScopedScanSuccess scanSuccess(state.mediaDatabase, stableKey);
    fileInfo.id = state.mediaDatabase
        ? state.mediaDatabase->GetOrCreateStableId(stableKey)
        : state.nextId++;
    fileInfo.parentId = parentId;
    fileInfo.path = path;
    fileInfo.isFolder = false;
    fileInfo.mimeType = mime;
    fileInfo.upnpClass = uclass;

    if (IsRemoteMediaUrl(path)) {
        fileInfo.sizeBytes = ProbeRemoteContentLength(path);
    } else {
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return;
        }
        fileInfo.sizeBytes = ((long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
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
    } else if (!IsRemoteMediaUrl(path) && uclass == L"object.item.videoItem") {
        std::wstring fileName = SourceDisplayName(path);
        wchar_t stemBuf[MAX_PATH];
        wcscpy_s(stemBuf, fileName.c_str());
        PathRemoveExtensionW(stemBuf);

        std::wstring folder = path;
        size_t slash = folder.find_last_of(L"\\/");
        folder = slash == std::wstring::npos ? L"." : folder.substr(0, slash);

        static const wchar_t* kSubExts[] = { L".srt", L".vtt", L".sub", L".ass", L".ssa", L".smi", L".txt" };
        for (const wchar_t* subExt : kSubExts) {
            std::wstring candidate = folder + L"\\" + stemBuf + subExt;
            DWORD attrs = GetFileAttributesW(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                fileInfo.subtitlePath = candidate;
                break;
            }
        }
    }

    SetAlbumArtIfExists(state, fileInfo);
    state.items.push_back(fileInfo);
    scanSuccess.Mark();
    if (allowArtistAlbumMirror) {
        AddArtistAlbumMirrorIfPresent(state, cfg, fileInfo, parentId);
    }
}

void MediaSources::ScanPlaylist(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& playlistPath, int parentId, int depth) {
    if (depth > 8) {
        LogPrint(L"%ls Skipping playlist due to recursion depth limit: %ls", kScanDepthLogCode, RedactUrlForLog(playlistPath).c_str());
        return;
    }
    for (const auto& entry : LoadPlaylistEntries(playlistPath)) {
        if (IsPlaylistSourcePath(entry.location)) {
            MediaItem playlistFolder;
            playlistFolder.id = AllocateContainerId(state, parentId, SourceStemName(entry.location), entry.location);
            playlistFolder.parentId = parentId;
            playlistFolder.path = entry.location;
            playlistFolder.title = entry.title.empty() ? SourceStemName(entry.location) : entry.title;
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            state.items.push_back(playlistFolder);
            ScanPlaylist(state, cfg, entry.location, playlistFolder.id, depth + 1);
            continue;
        }
        AddMediaFile(state, cfg, entry.location, parentId, entry.title, entry.subtitlePath);
    }
}

void MediaSources::ScanNetworkFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& folderUrl, int parentId, int depth) {
    if (depth > 8) {
        LogPrint(L"%ls Skipping network folder due to recursion depth limit: %ls", kScanDepthLogCode, RedactUrlForLog(folderUrl).c_str());
        return;
    }

    for (const auto& entry : ListRemoteDirectory(folderUrl)) {
        if (IsPlaylistSourcePath(entry.url)) {
            MediaItem playlistFolder;
            playlistFolder.id = AllocateContainerId(state, parentId, SourceStemName(entry.name), entry.url);
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
                MediaItem folderInfo;
                folderInfo.id = AllocateContainerId(state, parentId, SourceDisplayName(entry.name), entry.url);
                folderInfo.parentId = parentId;
                folderInfo.path = entry.url;
                folderInfo.title = SourceDisplayName(entry.name);
                folderInfo.isFolder = true;
                folderInfo.upnpClass = L"object.container.storageFolder";
                state.items.push_back(folderInfo);
                ScanNetworkFolder(state, cfg, entry.url, folderInfo.id, depth + 1);
            }
        }
    }
}

void MediaSources::ScanFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& rootPath, int parentId, int depth) {
    if (depth > 64) {
        LogPrint(L"%ls Skipping folder due to recursion depth limit: %ls", kScanDepthLogCode, rootPath.c_str());
        return;
    }
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = rootPath + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        if (state.mediaDatabase) {
            state.mediaDatabase->RecordScanError(BuildStableContainerKey(parentId, SourceDisplayName(rootPath), rootPath), L"Folder unavailable");
        }
        LogPrint(L"Skipping unreadable folder: %ls", rootPath.c_str());
        return;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)) continue;

        std::wstring fullPath = rootPath + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (cfg.flatFolderStyle) {
                ScanFolder(state, cfg, fullPath, parentId, depth + 1);
            } else {
                MediaItem folderInfo;
                folderInfo.id = AllocateContainerId(state, parentId, fd.cFileName, fullPath);
                folderInfo.parentId = parentId;
                folderInfo.path = fullPath;
                folderInfo.title = fd.cFileName;
                folderInfo.isFolder = true;
                folderInfo.upnpClass = L"object.container.storageFolder";
                state.items.push_back(folderInfo);

                ScanFolder(state, cfg, fullPath, folderInfo.id, depth + 1);
            }
        } else {
            if (IsPlaylistSourcePath(fullPath)) {
                MediaItem playlistFolder;
                playlistFolder.id = AllocateContainerId(state, parentId, SourceStemName(fullPath), fullPath);
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
    } while (FindNextFileW(hFind, &fd));
    
    FindClose(hFind);
}

std::vector<MediaItem> MediaSources::GetChildren(int parentId) {
    std::vector<MediaItem> res;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto found = m_childrenByParent.find(parentId);
        if (found != m_childrenByParent.end()) {
            for (size_t index : found->second) {
                if (index < m_items.size()) res.push_back(m_items[index]);
            }
        }
    }
    const bool sortByTitle = AppConfig.Snapshot().sortByTitle;
    if (sortByTitle) {
        std::sort(res.begin(), res.end(), [](const MediaItem& a, const MediaItem& b) {
            if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
            return NaturalLessWide(a.title, b.title);
        });
    }
    return res;
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

std::vector<MediaItem> MediaSources::GetAllItems() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items;
}

MediaItem MediaSources::GetItem(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_idToIndex.find(id);
    if (found != m_idToIndex.end() && found->second < m_items.size()) {
        return m_items[found->second];
    }
    MediaItem m = {};
    m.id = -1;
    return m;
}

int MediaSources::GetChildCount(int parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_childrenByParent.find(parentId);
    return found == m_childrenByParent.end() ? 0 : static_cast<int>(found->second.size());
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
