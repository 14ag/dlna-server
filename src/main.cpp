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
#include "../resources/resource.h"
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

// --- headless helpers (posix_main.cpp style) ---

std::atomic<bool> g_stopHeadless(false);

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        g_stopHeadless = true;
        return TRUE;
    }
    return FALSE;
}

void PrintHeadlessUsage() {
    std::wcerr << L"Usage: DLNA Server.exe --headless [--port 8200] [--name NAME] [--uuid UUID] [--debug] --source PATH_OR_URL [--source PATH_OR_URL...]\n";
    std::wcerr << L"Sources can be folders, playlist files (.m3u, .m3u8, .pls), smb:// URLs, or ftp:// URLs.\n";
}

int RunHeadless(LPWSTR* argv, int argc) {
    AppConfig.Load();
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--headless") == 0) continue;
        if (wcscmp(argv[i], L"--port") == 0 && i + 1 < argc) {
            int port = 0;
            if (!TryParsePortStrict(WideToUtf8(argv[++i]).c_str(), port)) {
                PrintHeadlessUsage();
                return 2;
            }
            AppConfig.port = port;
        }
        else if (wcscmp(argv[i], L"--name") == 0 && i + 1 < argc) AppConfig.serverName = argv[++i];
        else if (wcscmp(argv[i], L"--uuid") == 0 && i + 1 < argc) AppConfig.deviceUUID = argv[++i];
        else if (wcscmp(argv[i], L"--source") == 0 && i + 1 < argc) AppConfig.mediaSources.push_back({argv[++i], true});
        else if (wcscmp(argv[i], L"--debug") == 0) AppConfig.debugLog = true;
        else if (wcscmp(argv[i], L"--help") == 0) { PrintHeadlessUsage(); return 0; }
        else AppConfig.mediaSources.push_back({argv[i], true});
    }
    if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled) {
        PrintHeadlessUsage();
        return 2;
    }
    std::signal(SIGINT, [](int) { g_stopHeadless = true; });
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    if (!DLNAServer.Start()) return 1;
    while (!g_stopHeadless) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    DLNAServer.Stop();
    return 0;
}

// --- GUI helpers (existing) ---

bool HasCommandLineToken(const wchar_t* token) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], token) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

bool TryRunFirewallHelper(int& exitCode) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }

    bool configureFirewall = false;
    int port = 0;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--configure-firewall") == 0) {
            configureFirewall = true;
        } else if (wcscmp(argv[i], L"--port") == 0 && i + 1 < argc) {
            port = _wtoi(argv[++i]);
        }
    }

    LocalFree(argv);
    if (!configureFirewall) {
        return false;
    }

    AppConfig.Load();
    if (port <= 0) {
        port = AppConfig.port;
    }

    std::wstring message;
    exitCode = ConfigureFirewallAccessElevated(port, message) ? 0 : 1;
    return true;
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;

    // headless path: runs before any GUI/window infrastructure
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--headless") == 0) {
                int ret = RunHeadless(argv, argc);
                LocalFree(argv);
                return ret;
            }
        }
    }
    if (argv) LocalFree(argv);

    int helperExitCode = 0;
    if (TryRunFirewallHelper(helperExitCode)) {
        return helperExitCode;
    }

    // Check for single instance
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"dlna-server_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Find existing window and show it
        HWND hwndExisting = FindWindowW(L"dlna-server_Main", NULL);
        if (hwndExisting) {
            ShowWindow(hwndExisting, SW_RESTORE);
            SetForegroundWindow(hwndExisting);
        }
        return 0;
    }

    // Parse command line for --minimized
    bool startMinimized = HasCommandLineToken(L"--minimized");

    AppConfig.Load();

    MainWindow app;
    if (!app.Create(hInstance, startMinimized ? SW_HIDE : nCmdShow)) {
        return 0;
    }

    if (startMinimized) {
        PostMessageW(app.GetHwnd(), WM_COMMAND, IDC_BTN_STARTSTOP, 0);
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