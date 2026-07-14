#ifndef SETTINGS_HELP_H
#define SETTINGS_HELP_H
#include <string>
#include <vector>
struct SettingHelpInfo {
    std::wstring label;
    std::wstring meaning;
};
inline std::vector<SettingHelpInfo> GetSettingsHelpTable() {
    return {
        { L"Server name", L"UPnP friendly name advertised on the network" },
        { L"HTTP port", L"TCP port for the HTTP media server (default 0 = auto)" },
        { L"IP whitelist", L"Comma-separated IPs or CIDR ranges allowed to connect" },
        { L"Run on Windows startup", L"Automatically start the server when you log in" },
        { L"Debug log (write to file)", L"Write detailed debug information to a log file" },
        { L"Default playlist", L"Automatically enqueue selected items as a playlist" },
        { L"Add artist/album folders to audio", L"Organize audio by artist then album directory structure" },
        { L"Do not show All Media folders", L"Hide aggregate folders that show all content at once" },
        { L"Sort by title instead of file name", L"Display items sorted by embedded title metadata" },
        { L"Flat folders style", L"Show all files in a single list instead of a folder tree" },
        { L"Show file names instead of titles", L"Display the file system name instead of metadata title" },
        { L"Proxy streams", L"Relay media through the server instead of serving direct URLs" },
        { L"Background scan (auto-rescan on changes)", L"Watch media folders for changes and rescan automatically" }
    };
}
#endif
