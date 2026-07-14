#ifndef CLI_FLAGS_H
#define CLI_FLAGS_H
#include <string>
#include <vector>
struct CliFlagInfo {
    std::wstring flag;
    std::wstring meaning;
};
inline std::vector<CliFlagInfo> GetCliFlagTable() {
    return {
        { L"--headless, -h", L"Start without window (tray icon only)" },
        { L"--port N", L"HTTP port override (1-65535)" },
        { L"--name NAME", L"UPnP friendly server name override" },
        { L"--uuid UUID", L"Device UUID override" },
        { L"--source PATH", L"Add media source (folder, playlist, or URL)" },
        { L"--debug", L"Enable debug logging" },
        { L"--configure-firewall", L"Run firewall helper and exit" },
        { L"--help", L"Show this help and exit" }
    };
}
#endif
