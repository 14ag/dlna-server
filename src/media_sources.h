#ifndef MEDIA_SOURCES_H
#define MEDIA_SOURCES_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

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
    std::wstring albumArtPath;
    std::wstring albumArtMime;
};

struct MediaIndexState {
    std::vector<MediaItem> items;
    std::unordered_map<int, size_t> idToIndex;
    std::unordered_map<int, std::vector<size_t>> childrenByParent;
    std::unordered_set<std::wstring> duplicateKeys;
    int nextId = 1;
};

class MediaSources {
public:
    static MediaSources& Get();

    void Scan();
    std::vector<MediaItem> GetChildren(int parentId);
    std::vector<MediaItem> GetDescendants(int parentId);
    std::vector<MediaItem> GetAllItems();
    MediaItem GetItem(int id);
    int GetChildCount(int parentId);
    int GetSystemUpdateID();

private:
    MediaSources();
    void AddMediaFile(MediaIndexState& state, const std::wstring& path, int parentId, const std::wstring& titleOverride = L"", const std::wstring& subtitleOverride = L"", bool allowArtistAlbumMirror = true);
    void ScanPlaylist(MediaIndexState& state, const std::wstring& playlistPath, int parentId);
    void ScanNetworkFolder(MediaIndexState& state, const std::wstring& folderUrl, int parentId, int depth);
    void ScanFolder(MediaIndexState& state, const std::wstring& rootPath, int parentId);
    bool IsAllowedExtension(const std::wstring& ext, std::wstring& mime, std::wstring& uclass);
    static void BuildIndexes(MediaIndexState& state);
    void SwapScannedState(MediaIndexState&& state);

    std::mutex m_mutex;
    std::vector<MediaItem> m_items;
    std::unordered_map<int, size_t> m_idToIndex;
    std::unordered_map<int, std::vector<size_t>> m_childrenByParent;
    std::atomic<int> m_systemUpdateId;
};

#define AppMedia MediaSources::Get()

#endif // MEDIA_SOURCES_H
