#include <windows.h>
#include "mainwindow.h"
#include "config.h"
#include "../resources/resource.h"
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
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
    bool startMinimized = false;
    if (wcsstr(pCmdLine, L"--minimized") != NULL) {
        startMinimized = true;
    }

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
