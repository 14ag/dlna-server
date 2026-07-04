#ifndef MEDIA_SOURCES_H
#define MEDIA_SOURCES_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <utility>
#include <unordered_map>
#include <unordered_set>

struct ConfigSnapshot;
class MediaDatabase;

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
    std::unordered_map<std::wstring, int> containerKeys;
    std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> albumArtByDirectory;
    std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> folderAlbumArt;
    std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> perStemAlbumArt;
    std::unordered_set<std::wstring> duplicateKeys;
    MediaDatabase* mediaDatabase = nullptr;
    int nextId = 1;
};

class MediaSources {
public:
    static MediaSources& Get();

    void Scan();
    std::vector<MediaItem> GetChildren(int parentId);
    std::vector<MediaItem> GetDescendants(int parentId);
    MediaItem GetItem(int id);
    std::unordered_map<int, int> GetChildCounts(const std::vector<MediaItem>& items);
    int GetSystemUpdateID();

    enum class GetChildrenResult {
        Success,
        NotFound,
        NotAContainer
    };
    GetChildrenResult TryGetChildren(int objId, std::vector<MediaItem>& out);

private:
    MediaSources();
    void AddMediaFile(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& path, int parentId, const std::wstring& titleOverride = L"", const std::wstring& subtitleOverride = L"", bool allowArtistAlbumMirror = true);
    void AddHlsStreamItem(MediaIndexState& state, const std::wstring& path, int parentId, const std::wstring& titleOverride = L"");
    void ScanPlaylist(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& playlistPath, int parentId, int depth = 0);
    void ScanNetworkFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& folderUrl, int parentId, int depth);
    void ScanFolder(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& rootPath, int parentId, int depth = 0);
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
