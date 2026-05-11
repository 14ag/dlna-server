#include "config.h"
#include "netutils.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cctype>
#include <stdio.h>

namespace {
std::string TrimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
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
        std::string value = trimmed.substr(eq + 1);
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
}

Config& Config::Get() {
    static Config instance;
    return instance;
}

Config::Config() : port(8200), fileServerPort(8201), flatFolderStyle(false), showFileNamesInsteadOfTitles(false),
    proxyStreams(false), sortByTitle(false), doNotShowAllMediaFolders(false), addArtistAlbumFolders(false),
    debugLog(false), runOnBoot(false) {
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

void Config::SetRunOnBoot(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring val = std::wstring(L"\"") + exePath + L"\" --minimized";
            RegSetValueExW(hKey, L"WinDLNAServer", 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.length() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"WinDLNAServer");
        }
        RegCloseKey(hKey);
    }
}

void Config::Load() {
    std::wstring path = GetConfigPath();
    auto values = ReadConfigFile(path);

    serverName = Utf8ToWide(values.count("ServerName") ? values["ServerName"] : "WinDLNA Server");
    if (serverName.empty()) {
        serverName = L"WinDLNA Server";
    }

    port = ParseIntOrDefault(values, "Port", 8200);
    fileServerPort = ParseIntOrDefault(values, "FileServerPort", 8201);
    flatFolderStyle = ParseIntOrDefault(values, "FlatFolderStyle", 0) != 0;
    showFileNamesInsteadOfTitles = ParseIntOrDefault(values, "ShowFileNamesInsteadOfTitles", 0) != 0;
    proxyStreams = ParseIntOrDefault(values, "ProxyStreams", 0) != 0;
    sortByTitle = ParseIntOrDefault(values, "SortByTitle", 0) != 0;
    doNotShowAllMediaFolders = ParseIntOrDefault(values, "DoNotShowAllMediaFolders", 0) != 0;
    addArtistAlbumFolders = ParseIntOrDefault(values, "AddArtistAlbumFolders", 0) != 0;
    debugLog = ParseIntOrDefault(values, "DebugLog", 0) != 0;
    runOnBoot = ParseIntOrDefault(values, "RunOnBoot", 0) != 0;

    ipWhiteList = Utf8ToWide(values.count("IPWhiteList") ? values["IPWhiteList"] : "");
    deviceUUID = Utf8ToWide(values.count("DeviceUUID") ? values["DeviceUUID"] : "");
    if (deviceUUID.empty()) {
        deviceUUID = GenerateUUID();
        Save();
    }

    mediaSources.clear();
    std::wstring sourcesStr = Utf8ToWide(values.count("MediaSources") ? values["MediaSources"] : "");
    size_t pos = 0;
    while ((pos = sourcesStr.find(L"|")) != std::wstring::npos) {
        std::wstring token = sourcesStr.substr(0, pos);
        if (!token.empty()) mediaSources.push_back({ token, true });
        sourcesStr.erase(0, pos + 1);
    }
    if (!sourcesStr.empty()) mediaSources.push_back({ sourcesStr, true });
}

void Config::Save() {
    std::wstring path = GetConfigPath();

    std::wstring sourcesStr;
    for (size_t i = 0; i < mediaSources.size(); i++) {
        sourcesStr += mediaSources[i].path;
        if (i < mediaSources.size() - 1) sourcesStr += L"|";
    }

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
    ss << "IPWhiteList=" << WideToUtf8(ipWhiteList) << "\n";
    ss << "DeviceUUID=" << WideToUtf8(deviceUUID) << "\n";
    ss << "MediaSources=" << WideToUtf8(sourcesStr) << "\n";

    FILE* fp = NULL;
    if (_wfopen_s(&fp, path.c_str(), L"wb") == 0 && fp) {
        static const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        fwrite(bom, 1, sizeof(bom), fp);
        std::string content = ss.str();
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
    }

    SetRunOnBoot(runOnBoot);
}
