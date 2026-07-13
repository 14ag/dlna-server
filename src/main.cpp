#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include "mainwindow.h"
#include "config.h"
#include "dlna_utils.h"
#include "firewall_access.h"
#include "log.h"
#include "netutils.h"
#include "server.h"
#include "playlist_scan_concurrency.h"
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
    std::wcerr << L"Usage: DLNA Server.exe [--headless] [--port N] [--name NAME] [--uuid UUID] [--debug] --source PATH_OR_URL [--source PATH_OR_URL...]\n";
    std::wcerr << L"  --headless, -h    Start without window (tray icon only)\n";
    std::wcerr << L"  --port N          HTTP port override (1-65535)\n";
    std::wcerr << L"  --name NAME       UPnP friendly server name override\n";
    std::wcerr << L"  --uuid UUID       Device UUID override\n";
    std::wcerr << L"  --source PATH     Add media source (folder, playlist, or URL)\n";
    std::wcerr << L"  --debug           Enable debug logging\n";
    std::wcerr << L"  --configure-firewall  Run firewall helper and exit\n";
    std::wcerr << L"  --help            Show this help and exit\n";
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
            runtimeSources.push_back(argv[++i]);
        } else if (wcscmp(argv[i], L"--debug") == 0) {
            debugFlag = true;
        } else if (wcscmp(argv[i], L"--print-scan-concurrency") == 0 && i + 1 < argc) {
            size_t n = static_cast<size_t>(_wtoi(argv[++i]));
            std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
            LocalFree(argv);
            return 0;
        } else {
            runtimeSources.push_back(argv[i]);
        }
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
    for (const auto& src : runtimeSources) {
        AppConfig.mediaSources.push_back({src});
    }

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

    MainWindow app;
    if (!app.Create(hInstance, startHeadless ? SW_HIDE : nCmdShow, startHeadless)) {
        return 0;
    }

    HWND hwndMain = app.GetHwnd();
    g_hwndMainForConsole = hwndMain;
    if (startHeadless) {
        if (!AppConfig.debugLog) {
            // A leading \n forces the cursor to column 0 of a fresh line even if
            // cmd.exe's own prompt is still sitting on the current line (see the
            // AttachConsole(ATTACH_PARENT_PROCESS) race documented above this
            // function). Deliberately no trailing newline: leaving the cursor
            // immediately after the message, rather than one line below it,
            // avoids stranding an empty line before the shell redraws its prompt.
            std::wcout << L"\nserver is up" << std::flush;
            FreeConsole();
        }
        PostMessageW(hwndMain, WM_COMMAND, IDC_BTN_STARTSTOP, 0);
    }

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}
