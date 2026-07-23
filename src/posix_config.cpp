#include "config.h"
#include "dlna_utils.h"
#include "fs_backend.h"
#include "log.h"
#include "netutils.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <limits.h>
#include <random>
#include <sstream>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

namespace {

// Returns the directory containing the current executable by reading
// /proc/self/exe (Linux) or _NSGetExecutablePath (macOS).
std::string ExecutableDirectory() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string exe(path);
        const size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash);
    }
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) {
        path[len] = '\0';
        std::string exe(path);
        const size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash);
    }
#endif
    return ".";
}

std::string Trim(const std::string& value) {
    const char* ws = " \t\r\n";
    const size_t start = value.find_first_not_of(ws);
    if (start == std::string::npos) return {};
    const size_t end = value.find_last_not_of(ws);
    std::string result = value.substr(start, end - start + 1);
    if (result.size() >= 3 &&
        static_cast<unsigned char>(result[0]) == 0xEF &&
        static_cast<unsigned char>(result[1]) == 0xBB &&
        static_cast<unsigned char>(result[2]) == 0xBF) {
        result.erase(0, 3);
    }
    return result;
}

// Per the XDG Base Directory Specification [R1]: "$XDG_CONFIG_HOME defines
// the base directory relative to which user-specific configuration files
// should be written. If $XDG_CONFIG_HOME is either not set or empty, a
// default equal to $HOME/.config should be used." The spec additionally
// requires: "All paths set in these environment variables must be
// absolute. If an implementation encounters a relative path... it should
// consider the path invalid and ignore it."
std::string ResolveHomeDirectory() {
    if (const char* home = std::getenv("HOME"); home && *home) {
        return home;
    }
    // Fallback used by glibc/glib (g_get_home_dir()) when $HOME is unset:
    // consult the passwd database for the invoking user.
    if (struct passwd* pw = getpwuid(getuid()); pw && pw->pw_dir && *pw->pw_dir) {
        return pw->pw_dir;
    }
    // Last resort. Logged by the caller so this degraded mode is visible.
    return "/tmp";
}

std::string ResolveXdgConfigBase() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        xdgConfigHome && *xdgConfigHome && xdgConfigHome[0] == '/') {
        return xdgConfigHome;
    }
    return ResolveHomeDirectory() + "/.config";
}

std::string AppRootConfigPath() {
    const std::string configDir = ResolveXdgConfigBase() + "/dlna-server";
    std::error_code ec;
    std::filesystem::create_directories(configDir, ec);
    // create_directories() is a documented no-op (returns false, ec clear)
    // when the directory already exists -- safe to call on every launch,
    // no separate "first run" bootstrap step is needed.
    if (ec) {
        // Directory could not be created (e.g. degraded /tmp fallback on a
        // read-only root). Fall through and let the subsequent
        // WriteFileAtomicUtf8() failure produce the existing, already
        // logged "Config save failed" message rather than silently
        // guessing at another location.
    }
    return configDir + "/config.ini";
}

void MigrateLegacyConfigIfPresent(const std::wstring& newConfigPath) {
    if (FsExists(newConfigPath)) return; // never overwrite live XDG data

    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0) return;
    exePath[len] = '\0';
    std::string exe(exePath);
    size_t slash = exe.find_last_of('/');
    if (slash == std::string::npos) return;
    std::wstring legacyPath = Utf8ToWide(exe.substr(0, slash + 1) + "config.ini");

    if (!FsExists(legacyPath)) return;
    std::error_code ec;
    std::filesystem::copy_file(WideToUtf8(legacyPath), WideToUtf8(newConfigPath), ec);
    if (!ec) {
        LogPrint(L"Migrated legacy config from %ls to %ls", legacyPath.c_str(), newConfigPath.c_str());
    }
}

int ParseIntOrDefault(const std::string& value, int fallback) {
    if (value.empty()) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::wstring DefaultServerName() {
    char name[HOST_NAME_MAX + 1] = {};
    if (gethostname(name, sizeof(name) - 1) == 0) {
        name[sizeof(name) - 1] = '\0';
        if (name[0] != '\0') return Utf8ToWide(name);
    }
    const char* envName = std::getenv("HOSTNAME");
    if (envName && *envName) return Utf8ToWide(envName);
    return L"dlna-server";
}

}

// Searches standard locations for a bundled resource file (icon PNG, etc.)
// and returns the first existing path, or an empty string if not found.
// Resolution order: DLNA_RESOURCE_DIR (compile-time), exe-dir, installed
// system path (<exeDir>/../share/dlna-server/icons/), macOS bundle path
// (<exeDir>/../Resources/).
std::string ResolveBundledResourcePath(const std::string& fileName) {
    if (fileName.empty()) return {};
    const std::string exeDir = ExecutableDirectory();
    const std::vector<std::string> candidates = {
        std::string(DLNA_RESOURCE_DIR) + "/" + fileName,
        exeDir + "/" + fileName,
        exeDir + "/../share/dlna-server/icons/" + fileName,
        exeDir + "/../Resources/" + fileName,
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

Config& Config::Get() {
    static Config instance;
    return instance;
}

Config::Config()
    : serverName(DefaultServerName()),
      port(8200),
      fileServerPort(8201),
      flatFolderStyle(false),
      showFileNamesInsteadOfTitles(false),
      proxyStreams(false),
      sortByTitle(false),
      doNotShowAllMediaFolders(false),
      addArtistAlbumFolders(false),
      debugLog(false),
      ipWhiteList(L""),
      deviceUUID(L""),
      deviceManufacturer(L"dlna-server contributors"),
      deviceModelName(L"dlna-server"),
      presentationUrl(L"/"),
      runOnBoot(false),
      defaultPlaylistEnabled(false),
      defaultPlaylistPath(L""),
      backgroundScanEnabled(false) {
}

ConfigSnapshot Config::Snapshot() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return ConfigSnapshot{
        serverName,
        port,
        fileServerPort,
        flatFolderStyle,
        showFileNamesInsteadOfTitles,
        proxyStreams,
        sortByTitle,
        doNotShowAllMediaFolders,
        addArtistAlbumFolders,
        debugLog,
        ipWhiteList,
        deviceUUID,
        deviceManufacturer,
        deviceModelName,
        presentationUrl,
        runOnBoot,
        defaultPlaylistEnabled,
        defaultPlaylistPath,
        backgroundScanEnabled,
        mediaSources,
        networkInterfaceAllowList,
        m_hasRuntimeSourceOverride ? m_runtimeSourceOverride : mediaSources,
        m_hasRuntimeSourceOverride
    };
}

bool Config::IsDebugLogEnabled() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return debugLog;
}

int Config::GetPort() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return port;
}

bool Config::IsSortByTitleEnabled() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return sortByTitle;
}

bool Config::IsProxyStreamsEnabled() const {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return proxyStreams;
}

int ParsePortOrDefault(const std::string& value, int fallback) {
    int parsed = ParseIntOrDefault(value, fallback);
    return IsValidPort(parsed) ? parsed : fallback;
}

std::wstring Config::GetConfigPath() {
    return Utf8ToWide(AppRootConfigPath());
}

std::wstring Config::GenerateUUID() {
    std::random_device rd;
    std::seed_seq seed{
        rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd(),
        rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()
    };
    std::mt19937 gen(seed);
    std::uniform_int_distribution<unsigned int> dist(0, 15);
    std::uniform_int_distribution<unsigned int> dist2(8, 11);
    const char* hex = "0123456789abcdef";
    std::string uuid(36, '0');
    for (int i = 0; i < 36; ++i) {
        uuid[i] = (i == 8 || i == 13 || i == 18 || i == 23) ? '-' : hex[dist(gen)];
    }
    uuid[14] = '4';
    uuid[19] = hex[dist2(gen)];
    return Utf8ToWide(uuid);
}

std::wstring Config::GetDefaultPlaylistPath() {
    std::string path = WideToUtf8(GetConfigPath());
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return L"default.m3u";
    return Utf8ToWide(path.substr(0, slash + 1) + "default.m3u");
}

// Intentionally inert: the FLTK settings dialog no longer exposes a
// "run on startup" control on POSIX (see remediation workflow Phase 6E),
// so RunOnBoot in a legacy config.ini is read and persisted but never
// acted on. Re-implement this only alongside an explicit XDG autostart
// (.desktop) or launchd plist feature, not as a silent side effect here.
void Config::SetRunOnBoot(bool) {
}

void Config::Load() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    MigrateLegacyConfigIfPresent(GetConfigPath());
    std::ifstream file(WideToUtf8(GetConfigPath()), std::ios::binary);
    if (!file) {
        if (deviceUUID.empty()) deviceUUID = GenerateUUID();
        if (defaultPlaylistPath.empty()) defaultPlaylistPath = GetDefaultPlaylistPath();
        lock.unlock();
        Save();
        return;
    }

    std::string line;
    std::string section;
    bool needsSave = false;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        if (section != "Settings") continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = line.substr(eq + 1);
        if (key == "ServerName") serverName = Utf8ToWide(value);
        else if (key == "Port") port = ParsePortOrDefault(value, port);
        else if (key == "FileServerPort") fileServerPort = ParsePortOrDefault(value, fileServerPort);
        else if (key == "FlatFolderStyle") flatFolderStyle = ParseIntOrDefault(value, 0) != 0;
        else if (key == "ShowFileNamesInsteadOfTitles") showFileNamesInsteadOfTitles = ParseIntOrDefault(value, 0) != 0;
        else if (key == "ProxyStreams") proxyStreams = ParseIntOrDefault(value, 0) != 0;
        else if (key == "SortByTitle") sortByTitle = ParseIntOrDefault(value, 0) != 0;
        else if (key == "DoNotShowAllMediaFolders") doNotShowAllMediaFolders = ParseIntOrDefault(value, 0) != 0;
        else if (key == "AddArtistAlbumFolders") addArtistAlbumFolders = ParseIntOrDefault(value, 0) != 0;
        else if (key == "DebugLog") debugLog = ParseIntOrDefault(value, 0) != 0;
        else if (key == "RunOnBoot") runOnBoot = ParseIntOrDefault(value, 0) != 0;
        else if (key == "DefaultPlaylistEnabled") defaultPlaylistEnabled = ParseIntOrDefault(value, 0) != 0;
        else if (key == "BackgroundScanEnabled") backgroundScanEnabled = ParseIntOrDefault(value, 0) != 0;
        else if (key == "DefaultPlaylistPath") defaultPlaylistPath = Utf8ToWide(value);
        else if (key == "IPWhiteList") ipWhiteList = Utf8ToWide(value);
        else if (key == "DeviceUUID") deviceUUID = Utf8ToWide(value);
        else if (key == "DeviceManufacturer") deviceManufacturer = Utf8ToWide(value);
        else if (key == "DeviceModelName") deviceModelName = Utf8ToWide(value);
        else if (key == "PresentationURL") presentationUrl = Utf8ToWide(value);
        else if (key == "MediaSources") {
            mediaSources.clear();
            std::wstring sourcesRaw = Utf8ToWide(value);
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(sourcesRaw);
            if (parsedSources.empty() && sourcesRaw.find(L'|') != std::wstring::npos) {
                parsedSources = DecodeLegacyPipeDelimitedSources(sourcesRaw);
                needsSave = true;
            }
            for (const auto& token : parsedSources) {
                if (!token.empty()) mediaSources.push_back({token});
            }
        }
        else if (key == "NetworkInterfaceAllowList") networkInterfaceAllowList = Utf8ToWide(value);
    }
    if (serverName.empty()) serverName = DefaultServerName();
    if (defaultPlaylistPath.empty()) defaultPlaylistPath = GetDefaultPlaylistPath();
    if (deviceManufacturer.empty()) deviceManufacturer = L"dlna-server contributors";
    if (deviceModelName.empty()) deviceModelName = L"dlna-server";
    if (presentationUrl.empty()) presentationUrl = L"/";
    if (deviceUUID.empty()) {
        deviceUUID = GenerateUUID();
        needsSave = true;
    }
    lock.unlock();
    if (needsSave) {
        Save();
    }
}

void Config::Save() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    const std::wstring configPath = GetConfigPath();

    std::vector<std::wstring> sourcePaths;
    for (const auto& source : mediaSources) {
        sourcePaths.push_back(source.path);
    }
    std::wstring sourcesStr = BuildQuotedCommaList(sourcePaths);

    std::ostringstream out;
    out << "\xEF\xBB\xBF";
    out << "[Settings]\n";
    out << "ServerName=" << WideToUtf8(serverName) << "\n";
    out << "Port=" << port << "\n";
    out << "FileServerPort=" << fileServerPort << "\n";
    out << "FlatFolderStyle=" << (flatFolderStyle ? 1 : 0) << "\n";
    out << "ShowFileNamesInsteadOfTitles=" << (showFileNamesInsteadOfTitles ? 1 : 0) << "\n";
    out << "ProxyStreams=" << (proxyStreams ? 1 : 0) << "\n";
    out << "SortByTitle=" << (sortByTitle ? 1 : 0) << "\n";
    out << "DoNotShowAllMediaFolders=" << (doNotShowAllMediaFolders ? 1 : 0) << "\n";
    out << "AddArtistAlbumFolders=" << (addArtistAlbumFolders ? 1 : 0) << "\n";
    out << "DebugLog=" << (debugLog ? 1 : 0) << "\n";
    out << "RunOnBoot=" << (runOnBoot ? 1 : 0) << "\n";
    out << "DefaultPlaylistEnabled=" << (defaultPlaylistEnabled ? 1 : 0) << "\n";
    out << "DefaultPlaylistPath=" << WideToUtf8(defaultPlaylistPath) << "\n";
    out << "BackgroundScanEnabled=" << (backgroundScanEnabled ? 1 : 0) << "\n";
    out << "IPWhiteList=" << WideToUtf8(ipWhiteList) << "\n";
    out << "DeviceUUID=" << WideToUtf8(deviceUUID) << "\n";
    out << "DeviceManufacturer=" << WideToUtf8(deviceManufacturer) << "\n";
    out << "DeviceModelName=" << WideToUtf8(deviceModelName) << "\n";
    out << "PresentationURL=" << WideToUtf8(presentationUrl) << "\n";
    out << "MediaSources=" << WideToUtf8(sourcesStr) << "\n";
    out << "NetworkInterfaceAllowList=" << WideToUtf8(networkInterfaceAllowList) << "\n";

    const bool writeOk = WriteFileAtomicUtf8(configPath, out.str());
    lock.unlock();
    if (!writeOk) {
        LogPrint(L"Config save failed: %ls", configPath.c_str());
    }
}
