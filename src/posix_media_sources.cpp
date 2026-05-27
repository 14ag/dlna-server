#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
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
}

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
    m_items.push_back({0, -1, L"", L"Root", true, L"", L"object.container.storageFolder", 0});
    for (const auto& source : AppConfig.mediaSources) {
        if (!source.enabled) continue;
        if (!IsPlaylistSourcePath(source.path) && !IsNetworkShareUrl(source.path)) {
            fs::path path(WideToUtf8(source.path));
            if (!fs::exists(path)) continue;
        }
        MediaItem folder{};
        folder.id = m_nextId++;
        folder.parentId = 0;
        folder.path = source.path;
        folder.title = SourceDisplayName(source.path);
        folder.isFolder = true;
        folder.upnpClass = L"object.container.storageFolder";
        m_items.push_back(folder);

        if (IsPlaylistSourcePath(source.path)) {
            ScanPlaylist(source.path, folder.id);
        } else if (IsNetworkShareUrl(source.path)) {
            ScanNetworkFolder(source.path, folder.id, 0);
        } else {
            ScanFolder(source.path, folder.id);
        }
    }
    if (AppConfig.defaultPlaylistEnabled && !AppConfig.defaultPlaylistPath.empty()) {
        fs::path path(WideToUtf8(AppConfig.defaultPlaylistPath));
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            MediaItem playlistFolder{};
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
    ++m_systemUpdateId;
    LogPrint(L"Scanned %d media items.", static_cast<int>(m_items.size()));
}

void MediaSources::AddMediaFile(const std::wstring& pathText, int parentId, const std::wstring& titleOverride, const std::wstring& subtitleOverride) {
    std::wstring mime, uclass;
    std::wstring ext = SourceExtension(pathText);
    if (!IsAllowedExtension(ext, mime, uclass)) {
        if (IsRemoteMediaUrl(pathText) && ext.empty()) {
            mime = L"audio/mpeg";
            uclass = L"object.item.audioItem.musicTrack";
        } else {
            return;
        }
    }

    MediaItem file{};
    file.id = m_nextId++;
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
        file.sizeBytes = static_cast<long long>(fs::file_size(path, ec));
    }

    if (!titleOverride.empty()) {
        file.title = titleOverride;
    } else {
        file.title = AppConfig.showFileNamesInsteadOfTitles ? SourceDisplayName(pathText) : SourceStemName(pathText);
    }

    if (!subtitleOverride.empty()) {
        file.subtitlePath = subtitleOverride;
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

    m_items.push_back(file);
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
            MediaItem playlistFolder{};
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
            MediaItem folder{};
            folder.id = m_nextId++;
            folder.parentId = parentId;
            folder.path = entry.url;
            folder.title = SourceDisplayName(entry.name);
            folder.isFolder = true;
            folder.upnpClass = L"object.container.storageFolder";
            m_items.push_back(folder);
            ScanNetworkFolder(entry.url, folder.id, depth + 1);
        }
    }
}

void MediaSources::ScanFolder(const std::wstring& rootPath, int parentId) {
    fs::path root(WideToUtf8(rootPath));
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        const fs::path path = entry.path();
        if (entry.is_symlink(ec)) continue;
        if (IsHiddenPath(path) || !IsReadableEntry(entry)) continue;
        if (entry.is_directory(ec)) {
            MediaItem folder{};
            folder.id = m_nextId++;
            folder.parentId = parentId;
            folder.path = Utf8ToWide(path.u8string());
            folder.title = Utf8ToWide(path.filename().u8string());
            folder.isFolder = true;
            folder.upnpClass = L"object.container.storageFolder";
            m_items.push_back(folder);
            ScanFolder(folder.path, folder.id);
        } else if (entry.is_regular_file(ec)) {
            std::wstring fullPath = Utf8ToWide(path.u8string());
            if (IsPlaylistSourcePath(fullPath)) {
                MediaItem playlistFolder{};
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
    }
}

std::vector<MediaItem> MediaSources::GetChildren(int parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MediaItem> result;
    for (const auto& item : m_items) if (item.parentId == parentId) result.push_back(item);
    std::sort(result.begin(), result.end(), [](const MediaItem& a, const MediaItem& b) {
        if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
        return NaturalLessWide(a.title, b.title);
    });
    return result;
}

MediaItem MediaSources::GetItem(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& item : m_items) if (item.id == id) return item;
    MediaItem missing{};
    missing.id = -1;
    return missing;
}

int MediaSources::GetSystemUpdateID() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_systemUpdateId;
}
