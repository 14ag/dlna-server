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
        { L"--source \"pathA\",\"pathB\"", L"Add one or more media sources replaces the current session sources not saved to config" },
        { L"--kill-server, -k", L"Stop the running server and close the app overrides every other flag" },
        { L"--debug", L"Enable debug logging" },
        { L"--configure-firewall", L"Run firewall helper and exit" },
        { L"--help", L"Show this help and exit" }
    };
}
#endif
