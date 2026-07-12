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

bool HasHeadlessToken() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--headless") == 0 || wcscmp(argv[i], L"-h") == 0) {
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

    int helperExitCode = 0;
    if (TryRunFirewallHelper(helperExitCode)) {
        return helperExitCode;
    }

    // Hidden test hook: print scan concurrency for given N and exit
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (wcscmp(argv[i], L"--print-scan-concurrency") == 0 && i + 1 < argc) {
                    size_t n = static_cast<size_t>(_wtoi(argv[i + 1]));
                    std::cout << ComputePlaylistScanConcurrency(n) << std::endl;
                    LocalFree(argv);
                    return 0;
                }
            }
            LocalFree(argv);
        }
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

    bool startHeadless = HasHeadlessToken();

    // Attach console for headless mode output
    bool consoleAttached = false;
    FILE* fpOut = NULL;
    FILE* fpErr = NULL;
    if (startHeadless) {
        consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if (consoleAttached && GetLastError() == ERROR_ACCESS_DENIED) {
            // Already has console, skip redirect
        } else if (!consoleAttached && GetLastError() == ERROR_INVALID_HANDLE) {
            // No parent console (e.g., launched from Explorer), proceed silently
            consoleAttached = false;
        } else if (consoleAttached) {
            // Successfully attached, redirect stdout/stderr
            _wfreopen_s(&fpOut, L"CONOUT$", L"w", stdout);
            _wfreopen_s(&fpErr, L"CONOUT$", L"w", stderr);
        }

        AppConfig.Load();

        if (AppConfig.debugLog) {
            SetConsoleCtrlHandler(HeadlessConsoleCtrlHandler, TRUE);
        }
    } else {
        AppConfig.Load();
    }

    MainWindow app;
    if (!app.Create(hInstance, startHeadless ? SW_HIDE : nCmdShow, startHeadless)) {
        return 0;
    }

    HWND hwndMain = app.GetHwnd();
    g_hwndMainForConsole = hwndMain;
    if (startHeadless) {
        if (!AppConfig.debugLog) {
            std::wcout << L"server is up" << std::endl;
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