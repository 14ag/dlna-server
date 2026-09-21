#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "mainwindow.h"
#include "config.h"
#include "network_interface_policy.h"
#include "dlna_utils.h"
#include "http_common.h"
#include "httpserver.h"
#include "media_scan_common.h"
#include "firewall_access.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"
#include "server.h"
#include "media_sources.h"
#include "media_database.h"
#include "browse_page_cap.h"
#include "thread_guard.h"
#include "settings_restart.h"
#include "source_drop_target.h"
#include "startup_mode.h"
#include "access_key_hook.h"
#include "access_keys.h"
#include "hover_focus_state.h"
#include "input_gate.h"
#include "playlist_scan_concurrency.h"
#include "scan_cancellation.h"
#include "transmitfile_chunking.h"
#include "cli_flags.h"
#include "cli_print_hooks_win.h"
#include "media_source_file_types.h"
#include "contentdirectory.h"
#include "ssdp_common.h"
#include "ssdp.h"
#include "upnp_eventing.h"
#include "server_close_policy.h"
#include "health_check_policy.h"
#include "win_geometry_dump.h"
#include "close_pending_state.h"
#include "modal_stack.h"
#include "function_key_action.h"
#include "tray_notify.h"
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

    AppConfig.Load();

    {
        int printExitCode = 0;
        if (TryRunPrintHook(argc, argv, printExitCode)) {
            LocalFree(argv);
            return printExitCode;
        }
    }

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
            std::vector<MediaSource> immediateOverride;
            if (parsedSources.empty()) {
                runtimeSources.push_back(argv[i]);
                immediateOverride.push_back({argv[i]});
            } else {
                for (auto& parsed : parsedSources) {
                    runtimeSources.push_back(parsed);
                    immediateOverride.push_back({parsed});
                }
            }
            // Apply immediately for the normal Start path below
            // Print hooks read the same override through the shared prescan
            AppConfig.SetRuntimeSourceOverride(immediateOverride);
        } else if (wcscmp(argv[i], L"--kill-server") == 0 || wcscmp(argv[i], L"-k") == 0) {
            killServer = true;
        } else if (wcscmp(argv[i], L"--debug") == 0) {
            debugFlag = true;
        } else if (wcscmp(argv[i], L"--no-debug") == 0) {
            debugFlag = false;
            AppConfig.debugLog = false;
        } else if (wcscmp(argv[i], L"--dump-widget-geometry") == 0) {
            DumpWidgetGeometryFlag() = true;
        } else if (argv[i][0] == L'-') {
            std::wcerr << L"Unknown option: " << argv[i] << std::endl;
            PrintUsage();
            LocalFree(argv);
            return 2;
        } else {
            runtimeSources.push_back(argv[i]);
        }
    }

    if (killServer) {
        LocalFree(argv);
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            PostMessageW(hwndExisting, MainWindow::WM_KILL_SERVER, 0, 0);
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
    // CreateMutexW with bInitialOwner=TRUE returns ERROR_ALREADY_EXISTS
    // even when the previous owner has terminated (abandoned mutex).
    // Without the WAIT_ABANDONED_0 check below, a killed/crashed process
    // permanently prevents any new instance from starting until the zombie
    // process is manually killed. See workflow: stale-mutex-remediation.
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"dlna-server_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        DWORD waitResult = WaitForSingleObject(hMutex, 0);
        if (waitResult != WAIT_ABANDONED_0) {
            // Ask the existing instance to restore itself through
            // MainWindow::RestoreAndFocusMainWindow (see mainwindow.cpp), which
            // is the only code path that clears WS_EX_TOOLWINDOW when the
            // running instance was originally started with --headless. Do NOT
            // call ShowWindow/SetForegroundWindow directly here: this process
            // cannot call methods on the other process's MainWindow instance,
            // and skipping that code path is what left the window with a
            // permanently "lite" frame (no icon, no min/max buttons, tiny
            // close button) in the original bug.
            HWND hwndExisting = NULL;
            for (int attempt = 0; attempt < 25 && !hwndExisting; ++attempt) {
                hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
                if (!hwndExisting) Sleep(200);
            }
            if (hwndExisting) {
                PostMessageW(hwndExisting, MainWindow::WM_SHOW_EXISTING_INSTANCE, 0, 0);
            } else {
                // The mutex is held (another process is starting or running) but
                // its window never appeared within 5 seconds -- it may have
                // crashed between CreateMutexW and MainWindow::Create, or is
                // hung. Do not exit silently; this mirrors the POSIX side's
                // stderr message for the equivalent unreachable-peer case.
                std::wcerr << L"DLNA Server: an existing instance is starting or "
                              L"running but its window could not be found; exiting without action." << std::endl;
            }
            return 0;
        }
        // WAIT_ABANDONED_0: previous instance terminated without releasing
        // mutex. We now own it; continue with startup.
        ReleaseMutex(hMutex);
        // Re-acquire with WaitForSingleObject so the abandoned state is
        // cleared and normal ownership semantics apply going forward.
        WaitForSingleObject(hMutex, INFINITE);
    }

    // Attach console for headless mode output
    bool consoleAttached = false;
    FILE* reopened = NULL;
    if (startHeadless) {
        consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if (!consoleAttached && GetLastError() == ERROR_INVALID_HANDLE) {
            consoleAttached = false;
        } else if (consoleAttached) {
            _wfreopen_s(&reopened, L"CONOUT$", L"w", stdout);
            _wfreopen_s(&reopened, L"CONOUT$", L"w", stderr);
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
        if (msg.message == WM_KEYDOWN) {
            if (app.TryHandleFunctionKey(msg.wParam)) {
                continue;
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
