#include "settings_restart.h"
#include "config.h"

std::vector<std::wstring> DetermineSettingsRequiringRestart(const ConfigSnapshot& before,
                                                             const ConfigSnapshot& after) {
    std::vector<std::wstring> changed;
    if (before.port != after.port) changed.push_back(L"HTTP Port");
    if (before.serverName != after.serverName) changed.push_back(L"Server Name");
    if (before.ipWhiteList != after.ipWhiteList) changed.push_back(L"IP Whitelist");
    return changed;
}