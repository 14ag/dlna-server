#include "config.h"
#include "dlna_utils.h"
#include "netutils.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <random>
#include <sstream>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {
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

std::string AppRootConfigPath() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string exe(path);
        size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash + 1) + "config.ini";
    }
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) {
        path[len] = '\0';
        std::string exe(path);
        size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash + 1) + "config.ini";
    }
#endif
    return "config.ini";
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
      runOnBoot(false),
      defaultPlaylistEnabled(false),
      defaultPlaylistPath(L"") {
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
    std::mt19937 gen(rd());
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

void Config::SetRunOnBoot(bool) {
}

void Config::Load() {
    std::ifstream file(WideToUtf8(GetConfigPath()), std::ios::binary);
    if (!file) {
        if (deviceUUID.empty()) deviceUUID = GenerateUUID();
        if (defaultPlaylistPath.empty()) defaultPlaylistPath = GetDefaultPlaylistPath();
        Save();
        return;
    }

    std::string line;
    std::string section;
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
        else if (key == "DefaultPlaylistPath") defaultPlaylistPath = Utf8ToWide(value);
        else if (key == "IPWhiteList") ipWhiteList = Utf8ToWide(value);
        else if (key == "DeviceUUID") deviceUUID = Utf8ToWide(value);
        else if (key == "MediaSources") {
            mediaSources.clear();
            std::stringstream ss(value);
            std::string token;
            while (std::getline(ss, token, '|')) {
                if (!token.empty()) mediaSources.push_back({Utf8ToWide(token), true});
            }
        }
    }
    if (serverName.empty()) serverName = DefaultServerName();
    if (defaultPlaylistPath.empty()) defaultPlaylistPath = GetDefaultPlaylistPath();
    if (deviceUUID.empty()) {
        deviceUUID = GenerateUUID();
        Save();
    }
}

void Config::Save() {
    std::ofstream file(WideToUtf8(GetConfigPath()), std::ios::binary | std::ios::trunc);
    if (!file) return;

    std::wstring sourcesStr;
    for (size_t i = 0; i < mediaSources.size(); ++i) {
        sourcesStr += mediaSources[i].path;
        if (i + 1 < mediaSources.size()) sourcesStr += L"|";
    }

    file << "\xEF\xBB\xBF";
    file << "[Settings]\n";
    file << "ServerName=" << WideToUtf8(serverName) << "\n";
    file << "Port=" << port << "\n";
    file << "FileServerPort=" << fileServerPort << "\n";
    file << "FlatFolderStyle=" << (flatFolderStyle ? 1 : 0) << "\n";
    file << "ShowFileNamesInsteadOfTitles=" << (showFileNamesInsteadOfTitles ? 1 : 0) << "\n";
    file << "ProxyStreams=" << (proxyStreams ? 1 : 0) << "\n";
    file << "SortByTitle=" << (sortByTitle ? 1 : 0) << "\n";
    file << "DoNotShowAllMediaFolders=" << (doNotShowAllMediaFolders ? 1 : 0) << "\n";
    file << "AddArtistAlbumFolders=" << (addArtistAlbumFolders ? 1 : 0) << "\n";
    file << "DebugLog=" << (debugLog ? 1 : 0) << "\n";
    file << "RunOnBoot=" << (runOnBoot ? 1 : 0) << "\n";
    file << "DefaultPlaylistEnabled=" << (defaultPlaylistEnabled ? 1 : 0) << "\n";
    file << "DefaultPlaylistPath=" << WideToUtf8(defaultPlaylistPath) << "\n";
    file << "IPWhiteList=" << WideToUtf8(ipWhiteList) << "\n";
    file << "DeviceUUID=" << WideToUtf8(deviceUUID) << "\n";
    file << "MediaSources=" << WideToUtf8(sourcesStr) << "\n";
}
