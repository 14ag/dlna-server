#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "network_sources.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

MediaSources& MediaSources::Get() {
    static MediaSources instance;
    return instance;
}

MediaSources::MediaSources() : m_nextId(1), m_systemUpdateId(1) {
}

bool MediaSources::IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass) {
    MediaFormatInfo info;
    if (!GetMediaFormatForExtension(ext, info)) return false;
    mime = info.mimeType;
    uclass = info.upnpClass;
    return true;
}

void MediaSources::Scan() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items.clear();
    m_nextId = 1;

    // Root (0)
    MediaItem rootInfo;
    rootInfo.id = 0;
    rootInfo.parentId = -1;
    rootInfo.title = L"Root";
    rootInfo.isFolder = true;
    m_items.push_back(rootInfo);

    for (const auto& src : AppConfig.mediaSources) {
        if (!src.enabled) continue;

        MediaItem folderInfo;
        folderInfo.id = m_nextId++;
        folderInfo.parentId = 0;
        folderInfo.path = src.path;
        folderInfo.title = SourceDisplayName(src.path);
        folderInfo.isFolder = true;
        folderInfo.upnpClass = L"object.container.storageFolder";
        m_items.push_back(folderInfo);

        if (IsPlaylistSourcePath(src.path)) {
            ScanPlaylist(src.path, folderInfo.id);
        } else if (IsNetworkShareUrl(src.path)) {
            ScanNetworkFolder(src.path, folderInfo.id, 0);
        } else {
            ScanFolder(src.path, folderInfo.id);
        }
    }
    if (AppConfig.defaultPlaylistEnabled && !AppConfig.defaultPlaylistPath.empty()) {
        DWORD attrs = GetFileAttributesW(AppConfig.defaultPlaylistPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            MediaItem playlistFolder;
            playlistFolder.id = m_nextId++;
            playlistFolder.parentId = 0;
            playlistFolder.path = AppConfig.defaultPlaylistPath;
            playlistFolder.title = L"Default playlist";
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            m_items.push_back(playlistFolder);
            ScanPlaylist(AppConfig.defaultPlaylistPath, playlistFolder.id);
        }
    }
    m_systemUpdateId++;
    LogPrint(L"Scanned %d media items.", (int)m_items.size());
}

namespace {
std::wstring LowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring CanonicalMediaKey(const std::wstring& path) {
    if (IsRemoteMediaUrl(path)) return LowerWide(path);
    wchar_t full[MAX_PATH] = {};
    if (GetFullPathNameW(path.c_str(), MAX_PATH, full, NULL) > 0) {
        return LowerWide(full);
    }
    return LowerWide(path);
}

bool HasDuplicateMediaPath(const std::vector<MediaItem>& items, int parentId, const std::wstring& path) {
    const std::wstring key = CanonicalMediaKey(path);
    for (const auto& item : items) {
        if (!item.isFolder && item.parentId == parentId && CanonicalMediaKey(item.path) == key) {
            return true;
        }
    }
    return false;
}

void SetAlbumArtIfExists(MediaItem& item) {
    if (IsRemoteMediaUrl(item.path)) return;
    std::wstring folder = item.path;
    size_t slash = folder.find_last_of(L"\\/");
    folder = slash == std::wstring::npos ? L"." : folder.substr(0, slash);

    std::wstring fileName = SourceDisplayName(item.path);
    wchar_t stemBuf[MAX_PATH];
    wcscpy_s(stemBuf, fileName.c_str());
    PathRemoveExtensionW(stemBuf);

    std::vector<std::pair<std::wstring, std::wstring>> candidates = {
        { folder + L"\\folder.jpg", L"image/jpeg" },
        { folder + L"\\cover.jpg", L"image/jpeg" },
        { folder + L"\\album.jpg", L"image/jpeg" },
        { folder + L"\\" + stemBuf + L".jpg", L"image/jpeg" },
        { folder + L"\\" + stemBuf + L".png", L"image/png" },
    };
    for (const auto& candidate : candidates) {
        DWORD attrs = GetFileAttributesW(candidate.first.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            item.albumArtPath = candidate.first;
            item.albumArtMime = candidate.second;
            return;
        }
    }
}
}

void MediaSources::AddMediaFile(const std::wstring& path, int parentId, const std::wstring& titleOverride, const std::wstring& subtitleOverride) {
    if (HasDuplicateMediaPath(m_items, parentId, path)) return;

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

    MediaItem fileInfo;
    fileInfo.id = m_nextId++;
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

    if (!titleOverride.empty()) {
        fileInfo.title = titleOverride;
    } else if (AppConfig.showFileNamesInsteadOfTitles) {
        fileInfo.title = SourceDisplayName(path);
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

    SetAlbumArtIfExists(fileInfo);
    m_items.push_back(fileInfo);
}

void MediaSources::ScanPlaylist(const std::wstring& playlistPath, int parentId) {
    for (const auto& entry : LoadPlaylistEntries(playlistPath)) {
        AddMediaFile(entry.location, parentId, entry.title, entry.subtitlePath);
    }
}

void MediaSources::ScanNetworkFolder(const std::wstring& folderUrl, int parentId, int depth) {
    if (depth > 8) return;

    for (const auto& entry : ListRemoteDirectory(folderUrl)) {
        if (IsPlaylistSourcePath(entry.url)) {
            MediaItem playlistFolder;
            playlistFolder.id = m_nextId++;
            playlistFolder.parentId = parentId;
            playlistFolder.path = entry.url;
            playlistFolder.title = SourceStemName(entry.name);
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            m_items.push_back(playlistFolder);
            ScanPlaylist(entry.url, playlistFolder.id);
            continue;
        }

        std::wstring mime, uclass;
        if (IsAllowedExtension(SourceExtension(entry.url), mime, uclass)) {
            AddMediaFile(entry.url, parentId);
            continue;
        }

        if (entry.likelyDirectory) {
            MediaItem folderInfo;
            folderInfo.id = m_nextId++;
            folderInfo.parentId = parentId;
            folderInfo.path = entry.url;
            folderInfo.title = SourceDisplayName(entry.name);
            folderInfo.isFolder = true;
            folderInfo.upnpClass = L"object.container.storageFolder";
            m_items.push_back(folderInfo);
            ScanNetworkFolder(entry.url, folderInfo.id, depth + 1);
        }
    }
}

void MediaSources::ScanFolder(const std::wstring& rootPath, int parentId) {
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = rootPath + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)) continue;

        std::wstring fullPath = rootPath + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            MediaItem folderInfo;
            folderInfo.id = m_nextId++;
            folderInfo.parentId = parentId;
            folderInfo.path = fullPath;
            folderInfo.title = fd.cFileName;
            folderInfo.isFolder = true;
            folderInfo.upnpClass = L"object.container.storageFolder";
            m_items.push_back(folderInfo);

            ScanFolder(fullPath, folderInfo.id);
        } else {
            if (IsPlaylistSourcePath(fullPath)) {
                MediaItem playlistFolder;
                playlistFolder.id = m_nextId++;
                playlistFolder.parentId = parentId;
                playlistFolder.path = fullPath;
                playlistFolder.title = SourceStemName(fullPath);
                playlistFolder.isFolder = true;
                playlistFolder.upnpClass = L"object.container.storageFolder";
                m_items.push_back(playlistFolder);
                ScanPlaylist(fullPath, playlistFolder.id);
            } else {
                AddMediaFile(fullPath, parentId);
            }
        }
    } while (FindNextFileW(hFind, &fd));
    
    FindClose(hFind);
}

std::vector<MediaItem> MediaSources::GetChildren(int parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MediaItem> res;
    for (const auto& it : m_items) {
        if (it.parentId == parentId) res.push_back(it);
    }
    std::sort(res.begin(), res.end(), [](const MediaItem& a, const MediaItem& b) {
        if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
        return NaturalLessWide(a.title, b.title);
    });
    return res;
}

std::vector<MediaItem> MediaSources::GetAllItems() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items;
}

MediaItem MediaSources::GetItem(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& it : m_items) {
        if (it.id == id) return it;
    }
    MediaItem m = {};
    m.id = -1;
    return m;
}

int MediaSources::GetSystemUpdateID() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_systemUpdateId;
}
