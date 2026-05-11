#include "media_sources.h"
#include "config.h"
#include "log.h"
#include "netutils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

MediaSources& MediaSources::Get() {
    static MediaSources instance;
    return instance;
}

MediaSources::MediaSources() : m_nextId(1), m_systemUpdateId(1) {
}

bool MediaSources::IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass) {
    std::string value = WideToUtf8(ext);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == ".mp4" || value == ".m4v") { mime = L"video/mp4"; uclass = L"object.item.videoItem"; return true; }
    if (value == ".mkv") { mime = L"video/x-matroska"; uclass = L"object.item.videoItem"; return true; }
    if (value == ".avi") { mime = L"video/x-msvideo"; uclass = L"object.item.videoItem"; return true; }
    if (value == ".mov") { mime = L"video/quicktime"; uclass = L"object.item.videoItem"; return true; }
    if (value == ".mp3") { mime = L"audio/mpeg"; uclass = L"object.item.audioItem.musicTrack"; return true; }
    if (value == ".flac") { mime = L"audio/flac"; uclass = L"object.item.audioItem.musicTrack"; return true; }
    if (value == ".jpg" || value == ".jpeg") { mime = L"image/jpeg"; uclass = L"object.item.imageItem.photo"; return true; }
    if (value == ".png") { mime = L"image/png"; uclass = L"object.item.imageItem.photo"; return true; }
    return false;
}

void MediaSources::Scan() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items.clear();
    m_nextId = 1;
    m_items.push_back({0, -1, L"", L"Root", true, L"", L"object.container.storageFolder", 0});
    for (const auto& source : AppConfig.mediaSources) {
        if (!source.enabled) continue;
        fs::path path(WideToUtf8(source.path));
        if (!fs::exists(path)) continue;
        MediaItem folder{};
        folder.id = m_nextId++;
        folder.parentId = 0;
        folder.path = source.path;
        folder.title = Utf8ToWide(path.filename().u8string());
        folder.isFolder = true;
        folder.upnpClass = L"object.container.storageFolder";
        m_items.push_back(folder);
        ScanFolder(source.path, folder.id);
    }
    ++m_systemUpdateId;
    LogPrint(L"Scanned %d media items.", static_cast<int>(m_items.size()));
}

void MediaSources::ScanFolder(const std::wstring& rootPath, int parentId) {
    fs::path root(WideToUtf8(rootPath));
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        const fs::path path = entry.path();
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
            std::wstring mime, uclass;
            if (!IsAllowedExtension(Utf8ToWide(path.extension().u8string()), mime, uclass)) continue;
            MediaItem file{};
            file.id = m_nextId++;
            file.parentId = parentId;
            file.path = Utf8ToWide(path.u8string());
            file.isFolder = false;
            file.mimeType = mime;
            file.upnpClass = uclass;
            file.sizeBytes = static_cast<long long>(entry.file_size(ec));
            file.title = AppConfig.showFileNamesInsteadOfTitles ? Utf8ToWide(path.filename().u8string()) : Utf8ToWide(path.stem().u8string());
            m_items.push_back(file);
        }
    }
}

std::vector<MediaItem> MediaSources::GetChildren(int parentId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MediaItem> result;
    for (const auto& item : m_items) if (item.parentId == parentId) result.push_back(item);
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
