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
bool IsPlaylistSourcePath(const std::wstring& value);
bool IsHlsManifestText(const std::string& text);
bool IsHlsPlaylistSource(const std::wstring& playlistPath);
std::wstring RedactUrlForLog(const std::wstring& value);
std::wstring SourceExtension(const std::wstring& value);
std::wstring SourceDisplayName(const std::wstring& value);
std::wstring SourceStemName(const std::wstring& value);

std::vector<PlaylistEntry> LoadPlaylistEntries(const std::wstring& playlistPath);
std::vector<RemoteDirectoryEntry> ListRemoteDirectory(const std::wstring& directoryUrl);
long long ProbeRemoteContentLength(const std::wstring& url);

bool StreamRemoteContent(const std::wstring& url,
                         bool useRange,
                         long long startByte,
                         long long endByte,
                         const std::function<bool(const char*, size_t)>& writeChunk);

std::vector<long long> ProbeRemoteContentLengthBatch(const std::vector<std::wstring>& urls, size_t maxConcurrency = 4);

#endif // NETWORK_SOURCES_H
