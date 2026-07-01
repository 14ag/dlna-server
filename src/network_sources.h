#ifndef NETWORK_SOURCES_H
#define NETWORK_SOURCES_H

#include <functional>
#include <string>
#include <vector>

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

bool IsRemoteMediaUrl(const std::wstring& value);
bool IsNetworkShareUrl(const std::wstring& value);
bool IsPlaylistSourcePath(const std::wstring& value);
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

#endif // NETWORK_SOURCES_H
