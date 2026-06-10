#include <windows.h>
#include <shellapi.h>
#include "mainwindow.h"
#include "config.h"
#include "firewall_access.h"
#include "../resources/resource.h"
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
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
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;
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
