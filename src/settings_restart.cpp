#include "settings_restart.h"
#include "config.h"

std::vector<std::wstring> DetermineSettingsRequiringRestart(const ConfigSnapshot& before,
                                                             const ConfigSnapshot& after) {
    std::vector<std::wstring> changed;
    if (before.port != after.port) changed.push_back(L"HTTP Port");
    if (before.serverName != after.serverName) changed.push_back(L"Server Name");
    if (before.ipWhiteList != after.ipWhiteList) changed.push_back(L"IP Whitelist");
    if (before.debugLog != after.debugLog) changed.push_back(L"Debug Log");
    if (before.addArtistAlbumFolders != after.addArtistAlbumFolders) changed.push_back(L"Add Artist/Album Folders");
    if (before.doNotShowAllMediaFolders != after.doNotShowAllMediaFolders) changed.push_back(L"Do Not Show All Media Folders");
    if (before.sortByTitle != after.sortByTitle) changed.push_back(L"Sort By Title");
    if (before.flatFolderStyle != after.flatFolderStyle) changed.push_back(L"Flat Folders Style");
    if (before.showFileNamesInsteadOfTitles != after.showFileNamesInsteadOfTitles) changed.push_back(L"Show File Names Instead Of Titles");
    if (before.proxyStreams != after.proxyStreams) changed.push_back(L"Proxy Streams");
    if (before.backgroundScanEnabled != after.backgroundScanEnabled) changed.push_back(L"Background Scan");
    return changed;
}