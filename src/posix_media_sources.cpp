#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <utility>

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

std::wstring CanonicalMediaKey(const std::wstring& pathText) {
    if (IsRemoteMediaUrl(pathText)) {
        std::wstring value = pathText;
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }
    std::error_code ec;
    fs::path path(WideToUtf8(pathText));
    fs::path canonical = fs::weakly_canonical(path, ec);
    return Utf8ToWide((ec ? path : canonical).u8string());
}

std::wstring BuildDuplicateMediaKey(int parentId, const std::wstring& path) {
    return std::to_wstring(parentId) + L"\n" + CanonicalMediaKey(path);
}

void SetAlbumArtIfExists(MediaItem& item) {
    if (IsRemoteMediaUrl(item.path)) return;
    fs::path path(WideToUtf8(item.path));
    std::vector<std::pair<fs::path, std::wstring>> candidates = {
        { path.parent_path() / "folder.jpg", L"image/jpeg" },
        { path.parent_path() / "folder.JPG", L"image/jpeg" },
        { path.parent_path() / "Folder.jpg", L"image/jpeg" },
        { path.parent_path() / "cover.jpg", L"image/jpeg" },
        { path.parent_path() / "cover.JPG", L"image/jpeg" },
        { path.parent_path() / "Cover.jpg", L"image/jpeg" },
        { path.parent_path() / "album.jpg", L"image/jpeg" },
        { path.parent_path() / "album.JPG", L"image/jpeg" },
        { path.parent_path() / "Album.jpg", L"image/jpeg" },
        { path.parent_path() / "thumb.jpg", L"image/jpeg" },
        { path.parent_path() / "thumb.JPG", L"image/jpeg" },
        { path.parent_path() / "thumb.jpeg", L"image/jpeg" },
        { path.parent_path() / "thumb.JPEG", L"image/jpeg" },
        { path.parent_path() / (path.stem().u8string() + ".jpg"), L"image/jpeg" },
        { path.parent_path() / (path.stem().u8string() + ".jpeg"), L"image/jpeg" },
        { path.parent_path() / (path.stem().u8string() + ".png"), L"image/png" },
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate.first, ec)) {
            item.albumArtPath = Utf8ToWide(candidate.first.u8string());
            item.albumArtMime = candidate.second;
            return;
        }
    }
}

int FindOrAddContainer(MediaIndexState& state, int parentId, const std::wstring& title, const std::wstring& keyPath) {
    for (const auto& item : state.items) {
        if (item.isFolder && item.parentId == parentId && item.title == title && item.path == keyPath) {
            return item.id;
        }
    }

    MediaItem folder{};
    folder.id = state.nextId++;
    folder.parentId = parentId;
    folder.path = keyPath;
    folder.title = title;
    folder.isFolder = true;
    folder.upnpClass = L"object.container.storageFolder";
    state.items.push_back(folder);
    return folder.id;
}

void AddArtistAlbumMirrorIfPresent(MediaIndexState& state, const MediaItem& item, int sourceParentId) {
    if (!AppConfig.addArtistAlbumFolders || !AppConfig.flatFolderStyle || IsRemoteMediaUrl(item.path)) return;
    if (item.upnpClass != L"object.item.audioItem.musicTrack" && item.upnpClass != L"object.item.videoItem") return;

    fs::path path(WideToUtf8(item.path));
    fs::path albumPath = path.parent_path();
    fs::path artistPath = albumPath.parent_path();
    std::wstring album = Utf8ToWide(albumPath.filename().u8string());
    std::wstring artist = Utf8ToWide(artistPath.filename().u8string());
    if (artist.empty() || album.empty()) return;

    int artistId = FindOrAddContainer(state, sourceParentId, artist, Utf8ToWide(artistPath.u8string()));
    int albumId = FindOrAddContainer(state, artistId, album, Utf8ToWide(albumPath.u8string()));
    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(albumId, item.path)).second) return;

    MediaItem mirror = item;
    mirror.id = state.nextId++;
    mirror.parentId = albumId;
    state.items.push_back(mirror);
}
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
    MediaIndexState state;
    state.items.push_back({0, -1, L"", L"Root", true, L"", L"object.container.storageFolder", 0, L"", L"", L""});
    for (const auto& source : AppConfig.mediaSources) {
        if (!source.enabled) continue;
        if (!IsPlaylistSourcePath(source.path) && !IsNetworkShareUrl(source.path)) {
            fs::path path(WideToUtf8(source.path));
            if (!fs::exists(path)) continue;
        }
        MediaItem folder{};
        folder.id = state.nextId++;
        folder.parentId = 0;
        folder.path = source.path;
        folder.title = SourceDisplayName(source.path);
        folder.isFolder = true;
        folder.upnpClass = L"object.container.storageFolder";
        state.items.push_back(folder);

        if (IsPlaylistSourcePath(source.path)) {
            ScanPlaylist(state, source.path, folder.id);
        } else if (IsNetworkShareUrl(source.path)) {
            ScanNetworkFolder(state, source.path, folder.id, 0);
        } else {
            ScanFolder(state, source.path, folder.id);
        }
    }
    if (AppConfig.defaultPlaylistEnabled && !AppConfig.defaultPlaylistPath.empty()) {
        fs::path path(WideToUtf8(AppConfig.defaultPlaylistPath));
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            MediaItem playlistFolder{};
            playlistFolder.id = state.nextId++;
            playlistFolder.parentId = 0;
            playlistFolder.path = AppConfig.defaultPlaylistPath;
            playlistFolder.title = L"Default playlist";
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            state.items.push_back(playlistFolder);
            ScanPlaylist(state, AppConfig.defaultPlaylistPath, playlistFolder.id);
        }
    }
    int itemCount = static_cast<int>(state.items.size());
    BuildIndexes(state);
    SwapScannedState(std::move(state));
    m_systemUpdateId.fetch_add(1, std::memory_order_release);
    LogPrint(L"Scanned %d media items.", itemCount);
}

void MediaSources::AddMediaFile(MediaIndexState& state, const std::wstring& pathText, int parentId, const std::wstring& titleOverride, const std::wstring& subtitleOverride, bool allowArtistAlbumMirror) {
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

    if (!state.duplicateKeys.insert(BuildDuplicateMediaKey(parentId, pathText)).second) return;

    MediaItem file{};
    file.id = state.nextId++;
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

    if (AppConfig.showFileNamesInsteadOfTitles) file.title = SourceDisplayName(pathText);
    else if (!titleOverride.empty()) file.title = titleOverride;
    else file.title = SourceStemName(pathText);

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

    SetAlbumArtIfExists(file);
    state.items.push_back(file);
    if (allowArtistAlbumMirror) {
        AddArtistAlbumMirrorIfPresent(state, file, parentId);
    }
}

void MediaSources::ScanPlaylist(MediaIndexState& state, const std::wstring& playlistPath, int parentId) {
    for (const auto& entry : LoadPlaylistEntries(playlistPath)) {
        AddMediaFile(state, entry.location, parentId, entry.title, entry.subtitlePath);
    }
}

void MediaSources::ScanNetworkFolder(MediaIndexState& state, const std::wstring& folderUrl, int parentId, int depth) {
    if (depth > 8) return;

    for (const auto& entry : ListRemoteDirectory(folderUrl)) {
        if (IsPlaylistSourcePath(entry.url)) {
            MediaItem playlistFolder{};
            playlistFolder.id = state.nextId++;
            playlistFolder.parentId = parentId;
            playlistFolder.path = entry.url;
            playlistFolder.title = SourceStemName(entry.name);
            playlistFolder.isFolder = true;
            playlistFolder.upnpClass = L"object.container.storageFolder";
            state.items.push_back(playlistFolder);
            ScanPlaylist(state, entry.url, playlistFolder.id);
            continue;
        }

        std::wstring mime, uclass;
        if (IsAllowedExtension(SourceExtension(entry.url), mime, uclass)) {
            AddMediaFile(state, entry.url, parentId);
            continue;
        }

        if (entry.likelyDirectory) {
            if (AppConfig.flatFolderStyle) {
                ScanNetworkFolder(state, entry.url, parentId, depth + 1);
            } else {
                MediaItem folder{};
                folder.id = state.nextId++;
                folder.parentId = parentId;
                folder.path = entry.url;
                folder.title = SourceDisplayName(entry.name);
                folder.isFolder = true;
                folder.upnpClass = L"object.container.storageFolder";
                state.items.push_back(folder);
                ScanNetworkFolder(state, entry.url, folder.id, depth + 1);
            }
        }
    }
}

void MediaSources::ScanFolder(MediaIndexState& state, const std::wstring& rootPath, int parentId) {
    fs::path root(WideToUtf8(rootPath));
    std::error_code ec;
    fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    if (ec) {
        LogPrint(L"Skipping unreadable folder: %ls", rootPath.c_str());
        return;
    }
    while (it != end) {
        const fs::directory_entry entry = *it;
        ec.clear();
        const fs::path path = entry.path();
        bool skip = entry.is_symlink(ec) || IsHiddenPath(path) || !IsReadableEntry(entry);
        if (!skip && entry.is_directory(ec)) {
            if (AppConfig.flatFolderStyle) {
                ScanFolder(state, Utf8ToWide(path.u8string()), parentId);
            } else {
                MediaItem folder{};
                folder.id = state.nextId++;
                folder.parentId = parentId;
                folder.path = Utf8ToWide(path.u8string());
                folder.title = Utf8ToWide(path.filename().u8string());
                folder.isFolder = true;
                folder.upnpClass = L"object.container.storageFolder";
                state.items.push_back(folder);
                ScanFolder(state, folder.path, folder.id);
            }
        } else if (!skip && entry.is_regular_file(ec)) {
            std::wstring fullPath = Utf8ToWide(path.u8string());
            if (IsPlaylistSourcePath(fullPath)) {
                MediaItem playlistFolder{};
                playlistFolder.id = state.nextId++;
                playlistFolder.parentId = parentId;
                playlistFolder.path = fullPath;
                playlistFolder.title = SourceStemName(fullPath);
                playlistFolder.isFolder = true;
                playlistFolder.upnpClass = L"object.container.storageFolder";
                state.items.push_back(playlistFolder);
                ScanPlaylist(state, fullPath, playlistFolder.id);
            } else {
                AddMediaFile(state, fullPath, parentId);
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
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MediaItem> result;
    auto found = m_childrenByParent.find(parentId);
    if (found != m_childrenByParent.end()) {
        for (size_t index : found->second) {
            if (index < m_items.size()) result.push_back(m_items[index]);
        }
    }
    if (AppConfig.sortByTitle) {
        std::sort(result.begin(), result.end(), [](const MediaItem& a, const MediaItem& b) {
            if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
            return NaturalLessWide(a.title, b.title);
        });
    }
    return result;
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
    std::function<void(int)> appendChildren = [&](int currentParent) {
        auto found = childrenByParent.find(currentParent);
        if (found == childrenByParent.end()) return;

        std::vector<MediaItem> children;
        for (size_t index : found->second) {
            if (index < items.size()) children.push_back(items[index]);
        }
        if (AppConfig.sortByTitle) {
            std::sort(children.begin(), children.end(), [](const MediaItem& a, const MediaItem& b) {
                if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
                return NaturalLessWide(a.title, b.title);
            });
        }

        for (const auto& child : children) {
            result.push_back(child);
            if (child.isFolder) appendChildren(child.id);
        }
    };
    appendChildren(parentId);
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
    MediaItem missing{};
    missing.id = -1;
    return missing;
}

int MediaSources::GetChildCount(int parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_childrenByParent.find(parentId);
    return found == m_childrenByParent.end() ? 0 : static_cast<int>(found->second.size());
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
