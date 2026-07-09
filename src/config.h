#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <shared_mutex>
#ifdef _WIN32
#include <windows.h>
#endif

struct MediaSource {
    std::wstring path;
    bool enabled;
};

struct ConfigSnapshot {
    std::wstring serverName;
    int port;
    int fileServerPort;
    bool flatFolderStyle;
    bool showFileNamesInsteadOfTitles;
    bool proxyStreams;
    bool sortByTitle;
    bool doNotShowAllMediaFolders;
    bool addArtistAlbumFolders;
    bool debugLog;
    std::wstring ipWhiteList;
    std::wstring deviceUUID;
    std::wstring deviceManufacturer;
    std::wstring deviceModelName;
    std::wstring presentationUrl;
    bool runOnBoot;
    bool defaultPlaylistEnabled;
    std::wstring defaultPlaylistPath;
    std::vector<MediaSource> mediaSources;
};

class Config {
public:
    static Config& Get();

    void Load();
    void Save();
    ConfigSnapshot Snapshot() const;
    
    // Properties
    std::wstring serverName;
    int port;
    int fileServerPort;
    bool flatFolderStyle;
    bool showFileNamesInsteadOfTitles;
    bool proxyStreams;
    bool sortByTitle;
    bool doNotShowAllMediaFolders;
    bool addArtistAlbumFolders;
    bool debugLog;
    std::wstring ipWhiteList;
    std::wstring deviceUUID;
    std::wstring deviceManufacturer;
    std::wstring deviceModelName;
    std::wstring presentationUrl;
    bool runOnBoot;
    bool defaultPlaylistEnabled;
    std::wstring defaultPlaylistPath;
    
    std::vector<MediaSource> mediaSources;
    std::wstring GetDefaultPlaylistPath();
    std::wstring GetConfigPath();

    // Thread-safe mutation. Takes the same lock Snapshot()/Load()/Save() use,
    // so a Snapshot() call on another thread cannot observe a
    // partially-updated field or a std::vector mid-reallocation. Any write to
    // a Config field that can happen after DLNAServer.Start() (i.e. after the
    // background scan thread and HTTP worker threads exist) must go through
    // this instead of writing the public fields directly. One-time
    // command-line parsing before DLNAServer.Start() may continue to write
    // fields directly, since no other thread exists yet to race with it.
    template <typename F>
    void Mutate(F&& fn) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        fn(*this);
    }

    bool IsDebugLogEnabled() const;
    int GetPort() const;
    bool IsSortByTitleEnabled() const;
    bool IsProxyStreamsEnabled() const;

private:
    Config();
    std::wstring GenerateUUID();
    void SetRunOnBoot(bool enable);

    mutable std::shared_mutex m_mutex;
};

// Global config access
#define AppConfig Config::Get()

#endif // CONFIG_H
