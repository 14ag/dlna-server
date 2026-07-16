#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <shared_mutex>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

struct MediaSource {
    std::wstring path;
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
    bool backgroundScanEnabled;
    std::vector<MediaSource> mediaSources;
    std::wstring networkInterfaceAllowList;
    // the media sources that should actually be scanned right now
    // equals mediaSources unless a CLI supplied runtime override is active
    // never written by Save and never read by Load
    std::vector<MediaSource> effectiveMediaSources;
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
    bool backgroundScanEnabled;
    std::wstring defaultPlaylistPath;
    
    // INVARIANT: as of this writing, every direct (unlocked) read of this
    // field happens only from the UI thread (MainWindow::RefreshSourceList,
    // fltk_gui_main.cpp's equivalent), and every write to it happens only
    // via Mutate()/Load() also called from the UI thread. That is what
    // makes the direct-field-access pattern in those two files safe today
    // despite Config using a shared_mutex for its "official" thread-safe
    // surface (Snapshot()/Mutate()). If you are about to add ANY code path
    // that writes mediaSources from a background thread (an auto-import
    // feature, a remote-config-sync feature, etc.), every existing direct
    // read of this field must be converted to go through Snapshot() first
    // -- do not assume the existing direct reads are safe once this
    // invariant no longer holds.
    std::vector<MediaSource> mediaSources;
    std::wstring networkInterfaceAllowList;
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

    std::vector<MediaSource> GetRuntimeSourceOverride() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_runtimeSourceOverride;
    }

    bool HasRuntimeSourceOverride() const {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_hasRuntimeSourceOverride;
    }

    void SetRuntimeSourceOverride(std::vector<MediaSource> sources) {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_runtimeSourceOverride = std::move(sources);
        m_hasRuntimeSourceOverride = true;
    }

    void ClearRuntimeSourceOverride() {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_runtimeSourceOverride.clear();
        m_hasRuntimeSourceOverride = false;
    }

private:
    Config();
    std::wstring GenerateUUID();
    void SetRunOnBoot(bool enable);

    mutable std::shared_mutex m_mutex;
    std::vector<MediaSource> m_runtimeSourceOverride;
    bool m_hasRuntimeSourceOverride = false;
};

// Global config access
#define AppConfig Config::Get()

#endif // CONFIG_H
