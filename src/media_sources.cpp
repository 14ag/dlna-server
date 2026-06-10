#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "network_sources.h"
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

bool MediaSources::IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass) {
    MediaFormatInfo info;
    if (!GetMediaFormatForExtension(ext, info)) return false;
    mime = info.mimeType;
    uclass = info.upnpClass;
    return true;
}

void MediaSources::Scan() {
    const ConfigSnapshot cfg = AppConfig.Snapshot();
    MediaIndexState state;

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
        folderInfo.id = state.nextId++;
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
            playlistFolder.id = state.nextId++;
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
    m_systemUpdateId.fetch_add(1, std::memory_order_release);
    LogPrint(L"Scanned %d media items.", mediaItemCount);
}

namespace {
std::wstring CanonicalMediaKey(const std::wstring& path) {
    if (IsRemoteMediaUrl(path)) return ToLowerWide(path);
    wchar_t full[MAX_PATH] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH, full, NULL) > 0) {
        return ToLowerWide(full);
    }
    return ToLowerWide(path);
}

std::wstring BuildDuplicateMediaKey(int parentId, const std::wstring& path) {
    return std::to_wstring(parentId) + L"\n" + CanonicalMediaKey(path);
}

std::wstring ContainerLookupKey(int parentId, const std::wstring& title, const std::wstring& keyPath) {
    return std::to_wstring(parentId) + L"\n" + title + L"\n" + keyPath;
}

void SetAlbumArtIfExists(MediaIndexState& state, MediaItem& item) {
    if (IsRemoteMediaUrl(item.path)) return;
    std::wstring folder = item.path;
    size_t slash = folder.find_last_of(L"\\/");
    folder = slash == std::wstring::npos ? L"." : folder.substr(0, slash);

    auto cached = state.albumArtByDirectory.find(folder);
    if (cached != state.albumArtByDirectory.end()) {
        item.albumArtPath = cached->second.first;
        item.albumArtMime = cached->second.second;
        return;
    }

    std::wstring fileName = SourceDisplayName(item.path);
    wchar_t stemBuf[MAX_PATH];
    wcscpy_s(stemBuf, fileName.c_str());
    PathRemoveExtensionW(stemBuf);

    for (const auto& candidate : BuildAlbumArtCandidateNames(stemBuf)) {
        std::wstring candidatePath = folder + L"\\" + candidate.fileName;
        DWORD attrs = GetFileAttributesW(candidatePath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            item.albumArtPath = candidatePath;
            item.albumArtMime = candidate.mimeType;
            state.albumArtByDirectory[folder] = { item.albumArtPath, item.albumArtMime };
            return;
        }
    }
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
    folderInfo.id = state.nextId++;
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
    if (!cfg.addArtistAlbumFolders || !cfg.flatFolderStyle || IsRemoteMediaUrl(item.path)) return;
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
    mirror.id = state.nextId++;
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
            return;
        }
    }

    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, path)).second) return;

    MediaItem fileInfo;
    fileInfo.id = state.nextId++;
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
    if (allowArtistAlbumMirror) {
        AddArtistAlbumMirrorIfPresent(state, cfg, fileInfo, parentId);
    }
}

void MediaSources::ScanPlaylist(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& playlistPath, int parentId) {
    for (const auto& entry : LoadPlaylistEntries(playlistPath)) {
        AddMediaFile(state, cfg, entry.location, parentId, entry.title, entry.subtitlePath);
    }
}

void MediaSources::ScanNetworkFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& folderUrl, int parentId, int depth) {
    if (depth > 8) return;

    for (const auto& entry : ListRemoteDirectory(folderUrl)) {
        if (IsPlaylistSourcePath(entry.url)) {
            MediaItem playlistFolder;
            playlistFolder.id = state.nextId++;
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
                folderInfo.id = state.nextId++;
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
        LogPrint(L"Skipping folder due to recursion depth limit: %ls", rootPath.c_str());
        return;
    }
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = rootPath + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)) continue;

        std::wstring fullPath = rootPath + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (cfg.flatFolderStyle) {
                ScanFolder(state, cfg, fullPath, parentId, depth + 1);
            } else {
                MediaItem folderInfo;
                folderInfo.id = state.nextId++;
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
                playlistFolder.id = state.nextId++;
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
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MediaItem> res;
    auto found = m_childrenByParent.find(parentId);
    if (found != m_childrenByParent.end()) {
        for (size_t index : found->second) {
            if (index < m_items.size()) res.push_back(m_items[index]);
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
