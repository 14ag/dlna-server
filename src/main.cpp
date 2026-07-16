#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include "mainwindow.h"
#include "config.h"
#include "dlna_utils.h"
#include "firewall_access.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"
#include "server.h"
#include "settings_restart.h"
#include "source_drop_target.h"
#include "startup_mode.h"
#include "access_key_hook.h"
#include "access_keys.h"
#include "hover_focus_state.h"
#include "playlist_scan_concurrency.h"
#include "cli_flags.h"
#include "../resources/resource.h"
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

std::atomic<bool> g_headlessConsoleStop(false);
HWND g_hwndMainForConsole = NULL;

BOOL WINAPI HeadlessConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        g_headlessConsoleStop = true;
        if (g_hwndMainForConsole) {
            PostMessageW(g_hwndMainForConsole, WM_CLOSE, 0, 0);
        }
        return TRUE;
    }
    return FALSE;
}

void PrintUsage() {
    std::wcerr << L"Usage: DLNA Server.exe [--help]\n";
    std::wcerr << L"       DLNA Server.exe [OPTIONS...] --source \"pathA\",\"pathB\"\n";
    for (auto& entry : GetCliFlagTable()) {
        std::wcerr << L"  " << entry.flag << L"  " << entry.meaning << L"\n";
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    bool configureFirewall = false;
    bool startHeadless = false;
    bool showHelp = false;
    bool debugFlag = false;
    bool killServer = false;
    int portArg = 0;
    std::wstring runtimeName;
    std::wstring runtimeUUID;
    std::vector<std::wstring> runtimeSources;

    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--configure-firewall") == 0) {
            configureFirewall = true;
        } else if (wcscmp(argv[i], L"--headless") == 0 || wcscmp(argv[i], L"-h") == 0) {
            startHeadless = true;
        } else if (wcscmp(argv[i], L"--help") == 0) {
            showHelp = true;
        } else if (wcscmp(argv[i], L"--port") == 0 && i + 1 < argc) {
            ++i;
            if (!TryParsePortStrict(WideToUtf8(argv[i]), portArg)) portArg = 0;
        } else if (wcscmp(argv[i], L"--name") == 0 && i + 1 < argc) {
            runtimeName = argv[++i];
        } else if (wcscmp(argv[i], L"--uuid") == 0 && i + 1 < argc) {
            runtimeUUID = argv[++i];
        } else if (wcscmp(argv[i], L"--source") == 0 && i + 1 < argc) {
            ++i;
            std::vector<std::wstring> parsedSources = ParseQuotedCommaList(argv[i]);
            if (parsedSources.empty()) {
                runtimeSources.push_back(argv[i]);
            } else {
                for (auto& parsed : parsedSources) {
                    runtimeSources.push_back(parsed);
                }
            }
        } else if (wcscmp(argv[i], L"--kill-server") == 0 || wcscmp(argv[i], L"-k") == 0) {
            killServer = true;
        } else if (wcscmp(argv[i], L"--debug") == 0) {
            debugFlag = true;
        } else if (wcscmp(argv[i], L"--print-scan-concurrency") == 0 && i + 1 < argc) {
            size_t n = static_cast<size_t>(_wtoi(argv[++i]));
            std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-mnemonics") == 0 && i + 1 < argc) {
            std::wstring arg = argv[++i];
            std::vector<std::wstring> labels;
            size_t start = 0;
            for (size_t j = 0; j <= arg.size(); ++j) {
                if (j == arg.size() || arg[j] == L',') {
                    labels.push_back(arg.substr(start, j - start));
                    start = j + 1;
                }
            }
            std::vector<wchar_t> result = AssignMnemonics(labels);
            for (size_t j = 0; j < result.size(); ++j) {
                if (j > 0) std::cout << ",";
                if (result[j] != L'\0') {
                    std::cout << static_cast<char>(result[j]);
                }
            }
            std::cout << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-cue-state") == 0 && i + 1 < argc) {
            std::wstring seq = argv[++i];
            KeyboardCueState cs;
            for (wchar_t ch : seq) {
                if (ch == L'k' || ch == L'K') cs.OnKeyboardInput();
                else if (ch == L'm' || ch == L'M') cs.OnMouseButtonInput();
                std::cout << (cs.HideAccel() ? "1" : "0") << "," << (cs.HideFocus() ? "1" : "0") << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-hover-focus-state") == 0 && i + 1 < argc) {
            std::wstring seq = argv[++i];
            HoverFocusState state;
            size_t start = 0;
            while (start <= seq.size()) {
                size_t comma = seq.find(L',', start);
                std::wstring token = seq.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
                if (!token.empty()) {
                    wchar_t code = token[0];
                    int id = _wtoi(token.c_str() + 1);
                    if (code == L'e') state.OnMouseEnter(id);
                    else if (code == L'l') state.OnMouseLeave(id);
                    else if (code == L'f') state.OnFocusGained(id);
                    else if (code == L'b') state.OnFocusLost(id);
                    std::wcout << state.HighlightedControlId() << std::endl;
                }
                if (comma == std::wstring::npos) break;
                start = comma + 1;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-is-recognized-playlist") == 0 && i + 2 < argc) {
            std::wstring path = argv[++i];
            std::wstring textFilePath = argv[++i];
            std::ifstream file(WideToUtf8(textFilePath), std::ios::binary);
            std::ostringstream ss;
            ss << file.rdbuf();
            std::cout << (IsRecognizedPlaylistText(path, ss.str()) ? "1" : "0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-parse-quoted-comma-list") == 0 && i + 1 < argc) {
            for (const auto& field : ParseQuotedCommaList(argv[++i])) {
                std::wcout << field << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-decode-legacy-pipe-sources") == 0 && i + 1 < argc) {
            for (const auto& field : DecodeLegacyPipeDelimitedSources(argv[++i])) {
                std::wcout << field << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-start-headless") == 0 && i + 2 < argc) {
            bool explicitFlag = wcscmp(argv[++i], L"1") == 0;
            bool hasSources = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldStartHeadless(explicitFlag, hasSources) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-debug-log-requires-restart") == 0 && i + 2 < argc) {
            ConfigSnapshot before{};
            ConfigSnapshot after{};
            before.debugLog = wcscmp(argv[++i], L"1") == 0;
            after.debugLog = wcscmp(argv[++i], L"1") == 0;
            std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
            bool found = false;
            for (const auto& name : changed) {
                if (name == L"Debug Log") found = true;
            }
            std::wcout << (found ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-should-allow-source-drop") == 0 && i + 1 < argc) {
            bool busyOrRunning = wcscmp(argv[++i], L"1") == 0;
            std::wcout << (ShouldAllowSourceDrop(busyOrRunning) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-is-supported-source-path") == 0 && i + 1 < argc) {
            std::wcout << (IsSupportedLocalMediaOrPlaylistPath(argv[++i]) ? L"1" : L"0") << std::endl;
            LocalFree(argv);
            return 0;
        } else if (wcscmp(argv[i], L"--print-media-sources") == 0) {
            auto snap = AppConfig.Snapshot();
            for (const auto& src : snap.mediaSources) {
                std::wcout << src.path << std::endl;
            }
            LocalFree(argv);
            return 0;
        } else {
            runtimeSources.push_back(argv[i]);
        }
    }

    if (killServer) {
        LocalFree(argv);
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            PostMessageW(hwndExisting, MainWindow::WM_REQUEST_SHUTDOWN, 0, 0);
        }
        return 0;
    }

    if (showHelp) {
        PrintUsage();
        LocalFree(argv);
        return 0;
    }

    if (configureFirewall) {
        LocalFree(argv);
        AppConfig.Load();
        int port = portArg > 0 ? portArg : AppConfig.port;
        std::wstring message;
        return ConfigureFirewallAccessElevated(port, message) ? 0 : 1;
    }

    // Load config, then apply CLI overrides on top
    AppConfig.Load();

    if (portArg > 0 && portArg <= 65535) AppConfig.port = portArg;
    if (!runtimeName.empty()) AppConfig.serverName = runtimeName;
    if (!runtimeUUID.empty()) AppConfig.deviceUUID = runtimeUUID;
    if (debugFlag) AppConfig.debugLog = true;
    if (!runtimeSources.empty()) {
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            std::wstring payload = BuildQuotedCommaList(runtimeSources);
            COPYDATASTRUCT cds{};
            cds.dwData = MainWindow::kCopyDataSourceReplace;
            cds.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
            cds.lpData = const_cast<wchar_t*>(payload.c_str());
            SendMessageW(hwndExisting, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
            LocalFree(argv);
            return 0;
        }
        std::vector<MediaSource> overrideSources;
        for (const auto& src : runtimeSources) {
            overrideSources.push_back({src});
        }
        AppConfig.SetRuntimeSourceOverride(overrideSources);
    }

    startHeadless = ShouldStartHeadless(startHeadless, !runtimeSources.empty());

    LocalFree(argv);

    // Check for single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"dlna-server_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Ask the existing instance to restore itself through
        // MainWindow::RestoreAndFocusMainWindow (see mainwindow.cpp), which
        // is the only code path that clears WS_EX_TOOLWINDOW when the
        // running instance was originally started with --headless. Do NOT
        // call ShowWindow/SetForegroundWindow directly here: this process
        // cannot call methods on the other process's MainWindow instance,
        // and skipping that code path is what left the window with a
        // permanently "lite" frame (no icon, no min/max buttons, tiny
        // close button) in the original bug.
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            PostMessageW(hwndExisting, MainWindow::WM_SHOW_EXISTING_INSTANCE, 0, 0);
        }
        return 0;
    }

    // Attach console for headless mode output
    bool consoleAttached = false;
    FILE* fpOut = NULL;
    FILE* fpErr = NULL;
    if (startHeadless) {
        consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if (consoleAttached && GetLastError() == ERROR_ACCESS_DENIED) {
        } else if (!consoleAttached && GetLastError() == ERROR_INVALID_HANDLE) {
            consoleAttached = false;
        } else if (consoleAttached) {
            _wfreopen_s(&fpOut, L"CONOUT$", L"w", stdout);
            _wfreopen_s(&fpErr, L"CONOUT$", L"w", stderr);
        }

        if (AppConfig.debugLog) {
            SetConsoleCtrlHandler(HeadlessConsoleCtrlHandler, TRUE);
            if (consoleAttached) {
                SetConsoleEchoEnabled(true);
            }
        }
    }

    if (!InstallAccessKeyHook()) {
        // Non-fatal: keyboard cues will default to always-hidden Windows behaviour
    }

    MainWindow app;
    if (!app.Create(hInstance, startHeadless ? SW_HIDE : nCmdShow, startHeadless)) {
        RemoveAccessKeyHook();
        return 0;
    }

    HWND hwndMain = app.GetHwnd();
    g_hwndMainForConsole = hwndMain;
    if (startHeadless) {
        if (!AppConfig.debugLog) {
            std::wcout << L"\nserver is up" << std::flush;
            FreeConsole();
        }
        PostMessageW(hwndMain, WM_COMMAND, IDC_BTN_STARTSTOP, 0);
    }

    HWND hwndMainForNav = hwndMain;
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_CHAR && !(GetKeyState(VK_MENU) < 0)) {
            if (app.TryHandleAccessKeyChar(static_cast<wchar_t>(msg.wParam))) {
                continue; // consumed as an access key trigger, do not also dispatch it
            }
        }
        if (!IsDialogMessageW(hwndMainForNav, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    RemoveAccessKeyHook();
    return 0;
}
