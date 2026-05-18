#include "media_sources.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>

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

                // probe for companion subtitle file with matching stem
                // supported formats ordered by preference
                if (uclass == L"object.item.videoItem") {
                    wchar_t stemBuf[MAX_PATH];
                    wcscpy_s(stemBuf, fd.cFileName);
                    PathRemoveExtensionW(stemBuf);
                    static const wchar_t* kSubExts[] = { L".srt", L".vtt", L".sub", L".ass", L".ssa", L".smi", L".txt" };
                    for (const wchar_t* subExt : kSubExts) {
                        std::wstring candidate = rootPath + L"\\" + stemBuf + subExt;
                        DWORD attrs = GetFileAttributesW(candidate.c_str());
                        // check file exists and is not a directory
                        if (attrs != INVALID_FILE_ATTRIBUTES &&
                            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            fileInfo.subtitlePath = candidate;
                            break;
                        }
                    }
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
    std::sort(res.begin(), res.end(), [](const MediaItem& a, const MediaItem& b) {
        if (a.isFolder != b.isFolder) return a.isFolder && !b.isFolder;
        return NaturalLessWide(a.title, b.title);
    });
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
