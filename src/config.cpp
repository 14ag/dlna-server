#include "config.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <sstream>

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
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath))) {
        PathAppendW(szPath, L"WinDLNAServer");
        CreateDirectoryW(szPath, NULL);
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
    wchar_t buf[1024];

    GetPrivateProfileStringW(L"Settings", L"ServerName", L"WinDLNA Server", buf, 1024, path.c_str());
    serverName = buf;

    port = GetPrivateProfileIntW(L"Settings", L"Port", 8200, path.c_str());
    fileServerPort = GetPrivateProfileIntW(L"Settings", L"FileServerPort", 8201, path.c_str());

    flatFolderStyle = GetPrivateProfileIntW(L"Settings", L"FlatFolderStyle", 0, path.c_str());
    showFileNamesInsteadOfTitles = GetPrivateProfileIntW(L"Settings", L"ShowFileNamesInsteadOfTitles", 0, path.c_str());
    proxyStreams = GetPrivateProfileIntW(L"Settings", L"ProxyStreams", 0, path.c_str());
    sortByTitle = GetPrivateProfileIntW(L"Settings", L"SortByTitle", 0, path.c_str());
    doNotShowAllMediaFolders = GetPrivateProfileIntW(L"Settings", L"DoNotShowAllMediaFolders", 0, path.c_str());
    addArtistAlbumFolders = GetPrivateProfileIntW(L"Settings", L"AddArtistAlbumFolders", 0, path.c_str());
    debugLog = GetPrivateProfileIntW(L"Settings", L"DebugLog", 0, path.c_str());
    runOnBoot = GetPrivateProfileIntW(L"Settings", L"RunOnBoot", 0, path.c_str());

    GetPrivateProfileStringW(L"Settings", L"IPWhiteList", L"", buf, 1024, path.c_str());
    ipWhiteList = buf;

    GetPrivateProfileStringW(L"Settings", L"DeviceUUID", L"", buf, 1024, path.c_str());
    deviceUUID = buf;
    if (deviceUUID.empty()) {
        deviceUUID = GenerateUUID();
        WritePrivateProfileStringW(L"Settings", L"DeviceUUID", deviceUUID.c_str(), path.c_str());
    }

    // Media Sources
    GetPrivateProfileStringW(L"Settings", L"MediaSources", L"", buf, 1024, path.c_str());
    std::wstring sourcesStr(buf);
    
    mediaSources.clear();
    size_t pos = 0;
    while ((pos = sourcesStr.find(L"|")) != std::wstring::npos) {
        std::wstring token = sourcesStr.substr(0, pos);
        if(!token.empty()) mediaSources.push_back({token, true});
        sourcesStr.erase(0, pos + 1);
    }
    if(!sourcesStr.empty()) mediaSources.push_back({sourcesStr, true});
}

void Config::Save() {
    std::wstring path = GetConfigPath();

    WritePrivateProfileStringW(L"Settings", L"ServerName", serverName.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"Port", std::to_wstring(port).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"FileServerPort", std::to_wstring(fileServerPort).c_str(), path.c_str());
    
    WritePrivateProfileStringW(L"Settings", L"FlatFolderStyle", flatFolderStyle ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"ShowFileNamesInsteadOfTitles", showFileNamesInsteadOfTitles ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"ProxyStreams", proxyStreams ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"SortByTitle", sortByTitle ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"DoNotShowAllMediaFolders", doNotShowAllMediaFolders ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"AddArtistAlbumFolders", addArtistAlbumFolders ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"DebugLog", debugLog ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"Settings", L"RunOnBoot", runOnBoot ? L"1" : L"0", path.c_str());
    
    WritePrivateProfileStringW(L"Settings", L"IPWhiteList", ipWhiteList.c_str(), path.c_str());

    std::wstring sourcesStr;
    for(size_t i = 0; i < mediaSources.size(); i++) {
        sourcesStr += mediaSources[i].path;
        if(i < mediaSources.size() - 1) sourcesStr += L"|";
    }
    WritePrivateProfileStringW(L"Settings", L"MediaSources", sourcesStr.c_str(), path.c_str());

    SetRunOnBoot(runOnBoot);
}
