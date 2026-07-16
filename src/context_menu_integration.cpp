#include "context_menu_integration.h"
#include "dlna_utils.h"
#include "netutils.h"

#include <windows.h>

namespace {
const wchar_t* kVerbName = L"AddToDlnaServer";
const wchar_t* kVerbDisplayName = L"Add to DLNA Server";

std::wstring GetModulePathW() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return L"";
    }
    return path;
}

bool WriteVerbUnderKey(const std::wstring& baseKeyPath, const std::wstring& commandLine, std::wstring& message) {
    std::wstring verbKeyPath = baseKeyPath + L"\\shell\\" + kVerbName;
    std::wstring commandKeyPath = verbKeyPath + L"\\command";

    HKEY verbKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, verbKeyPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &verbKey, NULL) != ERROR_SUCCESS) {
        message = L"Could not create registry key under " + verbKeyPath;
        return false;
    }
    RegSetValueExW(verbKey, NULL, 0, REG_SZ,
                  reinterpret_cast<const BYTE*>(kVerbDisplayName),
                  static_cast<DWORD>((wcslen(kVerbDisplayName) + 1) * sizeof(wchar_t)));
    const wchar_t* multiSelect = L"Player";
    RegSetValueExW(verbKey, L"MultiSelectModel", 0, REG_SZ,
                  reinterpret_cast<const BYTE*>(multiSelect),
                  static_cast<DWORD>((wcslen(multiSelect) + 1) * sizeof(wchar_t)));
    RegCloseKey(verbKey);

    HKEY commandKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, commandKeyPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &commandKey, NULL) != ERROR_SUCCESS) {
        message = L"Could not create registry key under " + commandKeyPath;
        return false;
    }
    RegSetValueExW(commandKey, NULL, 0, REG_SZ,
                  reinterpret_cast<const BYTE*>(commandLine.c_str()),
                  static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(commandKey);
    return true;
}

void RemoveVerbUnderKey(const std::wstring& baseKeyPath) {
    std::wstring verbKeyPath = baseKeyPath + L"\\shell\\" + kVerbName;
    std::wstring commandKeyPath = verbKeyPath + L"\\command";
    RegDeleteKeyW(HKEY_CURRENT_USER, commandKeyPath.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, verbKeyPath.c_str());
}

std::vector<std::wstring> AllTargetBaseKeys() {
    std::vector<std::wstring> keys;
    keys.push_back(L"Software\\Classes\\Directory");
    for (const auto& ext : ContextMenuTargetExtensions()) {
        keys.push_back(L"Software\\Classes\\SystemFileAssociations\\" + ext);
    }
    return keys;
}
}

std::wstring BuildContextMenuCommandLine(const std::wstring& exePath) {
    return L"\"" + exePath + L"\" --headless --source %*";
}

std::vector<std::wstring> ContextMenuTargetExtensions() {
    return AllSupportedSourceExtensions();
}

bool EnsureContextMenuIntegration(bool enabled, std::wstring& message) {
    message.clear();
    std::wstring exePath = GetModulePathW();
    if (exePath.empty()) {
        message = L"Could not determine the executable path";
        return false;
    }
    std::wstring commandLine = BuildContextMenuCommandLine(exePath);

    if (!enabled) {
        for (const auto& baseKey : AllTargetBaseKeys()) {
            RemoveVerbUnderKey(baseKey);
        }
        message = L"Context menu entries removed";
        return true;
    }

    for (const auto& baseKey : AllTargetBaseKeys()) {
        if (!WriteVerbUnderKey(baseKey, commandLine, message)) {
            return false;
        }
    }
    message = L"Context menu entries added";
    return true;
}

bool IsContextMenuIntegrationRegistered() {
    std::wstring verbKeyPath = L"Software\\Classes\\Directory\\shell\\" + std::wstring(kVerbName);
    HKEY key = NULL;
    bool exists = RegOpenKeyExW(HKEY_CURRENT_USER, verbKeyPath.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS;
    if (key) {
        RegCloseKey(key);
    }
    return exists;
}
