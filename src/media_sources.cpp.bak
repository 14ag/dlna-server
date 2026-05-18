#include "media_sources.h"
#include "config.h"
#include "log.h"
#include <windows.h>
#include <shlwapi.h>

MediaSources& MediaSources::Get() {
    static MediaSources instance;
    return instance;
}

MediaSources::MediaSources() : m_nextId(1), m_systemUpdateId(1) {
}

bool MediaSources::IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass) {
    if (_wcsicmp(ext.c_str(), L".mp4") == 0 || _wcsicmp(ext.c_str(), L".m4v") == 0) {
        mime = L"video/mp4"; uclass = L"object.item.videoItem"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".mkv") == 0) {
        mime = L"video/x-matroska"; uclass = L"object.item.videoItem"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".avi") == 0) {
        mime = L"video/x-msvideo"; uclass = L"object.item.videoItem"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".mov") == 0) {
        mime = L"video/quicktime"; uclass = L"object.item.videoItem"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".mp3") == 0) {
        mime = L"audio/mpeg"; uclass = L"object.item.audioItem.musicTrack"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".flac") == 0) {
        mime = L"audio/flac"; uclass = L"object.item.audioItem.musicTrack"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0) {
        mime = L"image/jpeg"; uclass = L"object.item.imageItem.photo"; return true;
    }
    if (_wcsicmp(ext.c_str(), L".png") == 0) {
        mime = L"image/png"; uclass = L"object.item.imageItem.photo"; return true;
    }
    return false;
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
        
        wchar_t folderName[MAX_PATH];
        wcscpy_s(folderName, src.path.c_str());
        PathStripPathW(folderName);

        MediaItem folderInfo;
        folderInfo.id = m_nextId++;
        folderInfo.parentId = 0;
        folderInfo.path = src.path;
        folderInfo.title = folderName;
        folderInfo.isFolder = true;
        folderInfo.upnpClass = L"object.container.storageFolder";
        m_items.push_back(folderInfo);

        ScanFolder(src.path, folderInfo.id);
    }
    m_systemUpdateId++;
    LogPrint(L"Scanned %d media items.", (int)m_items.size());
}

void MediaSources::ScanFolder(const std::wstring& rootPath, int parentId) {
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = rootPath + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;

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
            LPCWSTR ext = PathFindExtensionW(fd.cFileName);
            std::wstring mime, uclass;
            if (IsAllowedExtension(ext, mime, uclass)) {
                MediaItem fileInfo;
                fileInfo.id = m_nextId++;
                fileInfo.parentId = parentId;
                fileInfo.path = fullPath;
                fileInfo.isFolder = false;
                fileInfo.mimeType = mime;
                fileInfo.upnpClass = uclass;
                fileInfo.sizeBytes = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                
                if (AppConfig.showFileNamesInsteadOfTitles) {
                    fileInfo.title = fd.cFileName;
                } else {
                    wchar_t name[MAX_PATH];
                    wcscpy_s(name, fd.cFileName);
                    PathRemoveExtensionW(name);
                    fileInfo.title = name;
                }
                m_items.push_back(fileInfo);
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
    return res;
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
