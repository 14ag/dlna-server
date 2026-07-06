#ifndef NETWORK_SOURCES_H
#define NETWORK_SOURCES_H

#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>

struct PlaylistEntry {
    std::wstring location;
    std::wstring title;
    std::wstring subtitlePath;
};

struct RemoteDirectoryEntry {
    std::wstring name;
    std::wstring url;
    bool likelyDirectory;
};

class ConcurrencyLimiter {
public:
    explicit ConcurrencyLimiter(size_t maxConcurrency)
        : m_maxConcurrency(maxConcurrency), m_activeCount(0) {}
    
    void Acquire() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_activeCount >= m_maxConcurrency) {
            m_cv.wait(lock);
        }
        ++m_activeCount;
    }
    
    void Release() {
        std::lock_guard<std::mutex> lock(m_mutex);
        --m_activeCount;
        if (m_activeCount < m_maxConcurrency) {
            m_cv.notify_one();
        }
    }
    
private:
    const size_t m_maxConcurrency;
    size_t m_activeCount;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

ConcurrencyLimiter& GetRemoteProbeLimiter();

bool IsRemoteMediaUrl(const std::wstring& value);
bool IsNetworkShareUrl(const std::wstring& value);
bool IsRemovedSmbSourcePath(const std::wstring& value);
bool IsPlaylistSourcePath(const std::wstring& value);
bool IsHlsManifestText(const std::string& text);
std::wstring RedactUrlForLog(const std::wstring& value);
std::wstring SourceExtension(const std::wstring& value);
std::wstring SourceDisplayName(const std::wstring& value);
std::wstring SourceStemName(const std::wstring& value);

// result of fetching a playlist exactly once
// fetchOk is false if the network or file read failed
// isHls is only meaningful when fetchOk is true
struct FetchedPlaylist {
    bool fetchOk = false;
    bool isHls = false;
    std::string text;
};

// fetches playlistPath one time and classifies it
// callers must not call ReadSourceText or IsHlsPlaylistSource separately after this
FetchedPlaylist FetchPlaylistOnce(const std::wstring& playlistPath);

// parses already fetched playlist text with no network or file access
std::vector<PlaylistEntry> ParseFetchedPlaylistText(const std::wstring& playlistPath, const std::string& text);

std::vector<PlaylistEntry> LoadPlaylistEntries(const std::wstring& playlistPath, bool* fetchFailed = nullptr);
std::vector<RemoteDirectoryEntry> ListRemoteDirectory(const std::wstring& directoryUrl);
long long ProbeRemoteContentLength(const std::wstring& url);

bool StreamRemoteContent(const std::wstring& url,
                         bool useRange,
                         long long startByte,
                         long long endByte,
                         const std::function<bool(const char*, size_t)>& writeChunk,
                         const std::vector<std::string>& reqHeaders = {},
                         const std::function<void(const std::string&, const std::string&)>& onHeader = nullptr);

std::vector<long long> ProbeRemoteContentLengthBatch(const std::vector<std::wstring>& urls, size_t maxConcurrency = 4);

#endif // NETWORK_SOURCES_H
