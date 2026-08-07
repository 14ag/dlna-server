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
        { L"--kill-server, -k", L"Stop the running server and close the app" },
        { L"--debug", L"Enable debug logging" },
        { L"--configure-firewall", L"Run firewall helper and exit" },
        { L"--help", L"Show this help and exit" }
    };
}

// true for any hidden test or debug only flag never a real user facing
// flag used to guarantee no code path can ever hold the console session
// or turn on console echo just because a test flag happened to also be
// combined with --debug on the same command line see the workflow doc
// task eight for the full requirement this decides
inline bool IsTestOnlyFlag(const std::wstring& arg) {
    return arg.rfind(L"--print-", 0) == 0 || arg.rfind(L"--dump-", 0) == 0;
}

inline bool IsTestOnlyFlag(const std::string& arg) {
    return arg.rfind("--print-", 0) == 0 || arg.rfind("--dump-", 0) == 0;
}
#endif
