#ifndef MEDIA_SOURCES_H
#define MEDIA_SOURCES_H

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

struct MediaItem {
    int id;
    int parentId;
    std::wstring path;
    std::wstring title;
    bool isFolder;
    std::wstring mimeType;
    std::wstring upnpClass;
    long long sizeBytes;
    // full path to companion subtitle file if one exists in the same folder
    // empty string means no subtitle was found
    std::wstring subtitlePath;
};

class MediaSources {
public:
    static MediaSources& Get();

    void Scan();
    std::vector<MediaItem> GetChildren(int parentId);
    MediaItem GetItem(int id);
    int GetSystemUpdateID();

private:
    MediaSources();
    void AddMediaFile(const std::wstring& path, int parentId, const std::wstring& titleOverride = L"");
    void ScanPlaylist(const std::wstring& playlistPath, int parentId);
    void ScanNetworkFolder(const std::wstring& folderUrl, int parentId, int depth);
    void ScanFolder(const std::wstring& rootPath, int parentId);
    bool IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass);

    std::mutex m_mutex;
    std::vector<MediaItem> m_items;
    int m_nextId;
    int m_systemUpdateId;
};

#define AppMedia MediaSources::Get()

#endif // MEDIA_SOURCES_H
