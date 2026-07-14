#ifndef NETWORK_SOURCES_H
#define NETWORK_SOURCES_H

#include "playlist_scan_concurrency.h"
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

AdaptiveConcurrencyLimiter& GetRemoteProbeLimiter();

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

// Result of fetching an HLS manifest specifically for re-serving to a DLNA
// renderer through this proxy (as opposed to FetchPlaylistOnce, which is
// used during library scanning to classify/enumerate playlist entries).
// text has had every URI line rewritten to an absolute URL via
// RewriteHlsManifestUrisToAbsolute so relative segment/variant references
// resolve correctly regardless of how the renderer reached this manifest.
struct HlsManifestFetchResult {
    bool fetchOk = false;
    std::string text;
};

// Fetches manifestUrl once and rewrites its relative URIs to absolute form.
// Used by both httpserver.cpp and posix_httpserver.cpp when serving a
// MediaItem whose mimeType is video/mpegurl.
HlsManifestFetchResult FetchHlsManifestForServing(const std::wstring& manifestUrl);

std::wstring ResolveRelativeUrl(const std::wstring &baseUrl, const std::wstring &relativeUrl);

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

#endif // NETWORK_SOURCES_H
