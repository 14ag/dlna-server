#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <mutex>
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

private:
    Config();
    std::wstring GetConfigPath();
    std::wstring GenerateUUID();
    void SetRunOnBoot(bool enable);

    mutable std::recursive_mutex m_mutex;
};

// Global config access
#define AppConfig Config::Get()

#endif // CONFIG_H
