#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <windows.h>

struct MediaSource {
    std::wstring path;
    bool enabled;
};

class Config {
public:
    static Config& Get();

    void Load();
    void Save();
    
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
    bool runOnBoot;
    
    std::vector<MediaSource> mediaSources;

private:
    Config();
    std::wstring GetConfigPath();
    std::wstring GenerateUUID();
    void SetRunOnBoot(bool enable);
};

// Global config access
#define AppConfig Config::Get()

#endif // CONFIG_H
