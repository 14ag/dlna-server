#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <stdio.h>

namespace {
std::string StripUtf8Bom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

std::string ConfigValueOrDefault(const std::unordered_map<std::string, std::string>& values, const char* key, const char* defaultValue) {
    auto it = values.find(key);
    return it == values.end() ? std::string(defaultValue) : StripUtf8Bom(it->second);
}

std::unordered_map<std::string, std::string> ParseConfigText(std::string text) {
    std::unordered_map<std::string, std::string> values;
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    std::istringstream stream(text);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        if (currentSection != "Settings") {
            continue;
        }

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = TrimAscii(trimmed.substr(0, eq));
        std::string value = StripUtf8Bom(trimmed.substr(eq + 1));
        values[key] = value;
    }

    return values;
}

std::unordered_map<std::string, std::string> ReadConfigFile(const std::wstring& path) {
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) {
        return {};
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return {};
    }

    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    fread(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);

    std::string text;
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const wchar_t* wideData = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
        size_t wideCount = (bytes.size() - 2) / sizeof(wchar_t);
        std::wstring wideText(wideData, wideCount);
        if (!wideText.empty() && wideText.back() == L'\0') {
            wideText.pop_back();
        }
        text = WideToUtf8(wideText);
    } else {
        text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    return ParseConfigText(text);
}

int ParseIntOrDefault(const std::unordered_map<std::string, std::string>& values, const char* key, int defaultValue) {
    auto it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        return defaultValue;
    }

    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

std::wstring DefaultServerName() {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    DWORD envLen = GetEnvironmentVariableW(L"COMPUTERNAME", name, size);
    if (envLen > 0 && envLen < size) {
        return name;
    }
    size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(name, &size) && size > 0) {
        return name;
    }
    return L"dlna-server";
}

}

Config& Config::Get() {
    static Config instance;
    return instance;
}

Config::Config() : port(8200), fileServerPort(8201), flatFolderStyle(false), showFileNamesInsteadOfTitles(false),
    proxyStreams(false), sortByTitle(false), doNotShowAllMediaFolders(false), addArtistAlbumFolders(false),
    debugLog(false),
    deviceManufacturer(L"dlna-server contributors"),
    deviceModelName(L"dlna-server"),
    presentationUrl(L"/"),
    runOnBoot(false),
    defaultPlaylistEnabled(false),
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

int ParsePortOrDefault(const std::unordered_map<std::string, std::string>& values, const char* key, int defaultValue) {
    int parsed = ParseIntOrDefault(values, key, defaultValue);
    return IsValidPort(parsed) ? parsed : defaultValue;
}

std::wstring Config::GetConfigPath() {
    wchar_t szPath[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, szPath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        PathRemoveFileSpecW(szPath);
        PathAppendW(szPath, L"config.ini");
        return std::wstring(szPath);
    }
    return L".\\config.ini";
}

std::wstring Config::GenerateUUID() {
    UUID uuid;
    UuidCreate(&uuid);
    RPC_WSTR str;
    UuidToStringW(&uuid, &str);
    std::wstring res((wchar_t*)str);
    RpcStringFreeW(&str);
    return res;
}

std::wstring Config::GetDefaultPlaylistPath() {
    std::wstring path = GetConfigPath();
    wchar_t buffer[MAX_PATH] = {};
    wcscpy_s(buffer, path.c_str());
    PathRemoveFileSpecW(buffer);
    PathAppendW(buffer, L"default.m3u");
    return buffer;
}

void Config::SetRunOnBoot(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring val = std::wstring(L"\"") + exePath + L"\" --headless";
        RegSetValueExW(hKey, L"dlna-server", 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.length() + 1) * sizeof(wchar_t)));
    } else {
            RegDeleteValueW(hKey, L"dlna-server");
        }
        RegCloseKey(hKey);
    }
}

void Config::Load() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::wstring path = GetConfigPath();
    auto values = ReadConfigFile(path);

    auto serverNameIt = values.find("ServerName");
    if (serverNameIt == values.end() || serverNameIt->second.empty()) {
        serverName = DefaultServerName();
    } else {
        serverName = Utf8ToWide(serverNameIt->second);
    }
    if (serverName.empty()) {
        serverName = DefaultServerName();
    }

    port = ParsePortOrDefault(values, "Port", 8200);
    fileServerPort = ParsePortOrDefault(values, "FileServerPort", 8201);
    flatFolderStyle = ParseIntOrDefault(values, "FlatFolderStyle", 0) != 0;
    showFileNamesInsteadOfTitles = ParseIntOrDefault(values, "ShowFileNamesInsteadOfTitles", 0) != 0;
    proxyStreams = ParseIntOrDefault(values, "ProxyStreams", 0) != 0;
    sortByTitle = ParseIntOrDefault(values, "SortByTitle", 0) != 0;
    doNotShowAllMediaFolders = ParseIntOrDefault(values, "DoNotShowAllMediaFolders", 0) != 0;
    addArtistAlbumFolders = ParseIntOrDefault(values, "AddArtistAlbumFolders", 0) != 0;
    debugLog = ParseIntOrDefault(values, "DebugLog", 0) != 0;
    runOnBoot = ParseIntOrDefault(values, "RunOnBoot", 0) != 0;
    defaultPlaylistEnabled = ParseIntOrDefault(values, "DefaultPlaylistEnabled", 0) != 0;
    backgroundScanEnabled = ParseIntOrDefault(values, "BackgroundScanEnabled", 0) != 0;

    ipWhiteList = Utf8ToWide(ConfigValueOrDefault(values, "IPWhiteList", ""));
    deviceUUID = Utf8ToWide(ConfigValueOrDefault(values, "DeviceUUID", ""));
    deviceManufacturer = Utf8ToWide(ConfigValueOrDefault(values, "DeviceManufacturer", "dlna-server contributors"));
    deviceModelName = Utf8ToWide(ConfigValueOrDefault(values, "DeviceModelName", "dlna-server"));
    presentationUrl = Utf8ToWide(ConfigValueOrDefault(values, "PresentationURL", "/"));
    defaultPlaylistPath = Utf8ToWide(ConfigValueOrDefault(values, "DefaultPlaylistPath", ""));
    if (defaultPlaylistPath.empty()) {
        defaultPlaylistPath = GetDefaultPlaylistPath();
    }
    if (deviceManufacturer.empty()) deviceManufacturer = L"dlna-server contributors";
    if (deviceModelName.empty()) deviceModelName = L"dlna-server";
    if (presentationUrl.empty()) presentationUrl = L"/";

    bool needsSave = false;
    if (deviceUUID.empty()) {
        deviceUUID = GenerateUUID();
        needsSave = true;
    }

    mediaSources.clear();
    std::wstring sourcesRaw = Utf8ToWide(ConfigValueOrDefault(values, "MediaSources", ""));
    std::vector<std::wstring> parsedSources = ParseQuotedCommaList(sourcesRaw);
    if (parsedSources.empty() && sourcesRaw.find(L'|') != std::wstring::npos) {
        // config file predates the quoted comma format upgrade it now
        parsedSources = DecodeLegacyPipeDelimitedSources(sourcesRaw);
        needsSave = true;
    }
    for (const auto& token : parsedSources) {
        if (!token.empty()) mediaSources.push_back({ token });
    }

    networkInterfaceAllowList = Utf8ToWide(ConfigValueOrDefault(values, "NetworkInterfaceAllowList", ""));

    lock.unlock();
    if (needsSave) Save();
}

void Config::Save() {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    std::wstring path = GetConfigPath();

    std::vector<std::wstring> sourcePaths;
    for (const auto& source : mediaSources) {
        sourcePaths.push_back(source.path);
    }
    std::wstring sourcesStr = BuildQuotedCommaList(sourcePaths);

    std::ostringstream ss;
    ss << "[Settings]\n";
    ss << "ServerName=" << WideToUtf8(serverName) << "\n";
    ss << "Port=" << port << "\n";
    ss << "FileServerPort=" << fileServerPort << "\n";
    ss << "FlatFolderStyle=" << (flatFolderStyle ? 1 : 0) << "\n";
    ss << "ShowFileNamesInsteadOfTitles=" << (showFileNamesInsteadOfTitles ? 1 : 0) << "\n";
    ss << "ProxyStreams=" << (proxyStreams ? 1 : 0) << "\n";
    ss << "SortByTitle=" << (sortByTitle ? 1 : 0) << "\n";
    ss << "DoNotShowAllMediaFolders=" << (doNotShowAllMediaFolders ? 1 : 0) << "\n";
    ss << "AddArtistAlbumFolders=" << (addArtistAlbumFolders ? 1 : 0) << "\n";
    ss << "DebugLog=" << (debugLog ? 1 : 0) << "\n";
    ss << "RunOnBoot=" << (runOnBoot ? 1 : 0) << "\n";
    ss << "DefaultPlaylistEnabled=" << (defaultPlaylistEnabled ? 1 : 0) << "\n";
    ss << "DefaultPlaylistPath=" << WideToUtf8(defaultPlaylistPath) << "\n";
    ss << "BackgroundScanEnabled=" << (backgroundScanEnabled ? 1 : 0) << "\n";
    ss << "IPWhiteList=" << WideToUtf8(ipWhiteList) << "\n";
    ss << "DeviceUUID=" << WideToUtf8(deviceUUID) << "\n";
    ss << "DeviceManufacturer=" << WideToUtf8(deviceManufacturer) << "\n";
    ss << "DeviceModelName=" << WideToUtf8(deviceModelName) << "\n";
    ss << "PresentationURL=" << WideToUtf8(presentationUrl) << "\n";
    ss << "MediaSources=" << WideToUtf8(sourcesStr) << "\n";
    ss << "NetworkInterfaceAllowList=" << WideToUtf8(networkInterfaceAllowList) << "\n";

    static const std::string bom = "\xEF\xBB\xBF";
    if (!WriteFileAtomicUtf8(path, bom + ss.str())) {
        LogPrint(L"Config save failed: %ls", path.c_str());
    }

    SetRunOnBoot(runOnBoot);
}
