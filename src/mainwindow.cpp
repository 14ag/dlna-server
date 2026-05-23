#include "mainwindow.h"
#include "../resources/resource.h"
#include "settingsdlg.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include "config.h"
#include "media_sources.h"
#include "server.h"

#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ID 1

MainWindow::MainWindow() : m_hwnd(NULL), m_hInstance(NULL), m_isRunning(false),
m_hBtnAdd(NULL), m_hBtnStartStop(NULL), m_hBtnSettings(NULL), m_hListSources(NULL) {
    m_hBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    m_hDarkBrush = CreateSolidBrush(RGB(45, 45, 48));
}

MainWindow::~MainWindow() {
    RemoveTrayIcon();
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hDarkBrush) DeleteObject(m_hDarkBrush);
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    const wchar_t CLASS_NAME[] = L"dlna-server_Main";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = m_hBgBrush;

    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        0, CLASS_NAME, L"dlna-server",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, this
    );

    if (m_hwnd == NULL) return false;

    // Create toolbar buttons (flat style)
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    m_hBtnAdd = CreateWindowExW(0, L"BUTTON", L"\x2795", // +
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_FLAT,
        650, 10, 30, 30, m_hwnd, (HMENU)IDC_BTN_ADD, hInstance, NULL);

    m_hBtnStartStop = CreateWindowExW(0, L"BUTTON", L"\x25B6", // Play
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_FLAT,
        690, 10, 30, 30, m_hwnd, (HMENU)IDC_BTN_STARTSTOP, hInstance, NULL);

    m_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"\x2699", // Gear
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_FLAT,
        730, 10, 30, 30, m_hwnd, (HMENU)IDC_BTN_SETTINGS, hInstance, NULL);

    if(hFont) {
        SendMessage(m_hBtnAdd, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hBtnStartStop, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(m_hBtnSettings, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    // Source ListBox
    m_hListSources = CreateWindowExW(0, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        0, 72, 800, 528, m_hwnd, (HMENU)IDC_LIST_SOURCES, hInstance, NULL);

    RefreshSourceList();

    AddTrayIcon();

    if (nCmdShow != SW_HIDE) {
        ShowWindow(m_hwnd, nCmdShow);
    }
    return true;
}

void MainWindow::SetStatus(bool running, const std::wstring& endpoint) {
    m_isRunning = running;
    m_statusEndpoint = endpoint;
    SendMessage(m_hBtnStartStop, WM_SETTEXT, 0, (LPARAM)(running ? L"\x25A0" : L"\x25B6")); // Stop / Play
    InvalidateRect(m_hwnd, NULL, TRUE);
}

void MainWindow::AddTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = m_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"dlna-server");

    Shell_NotifyIconW(NIM_ADD, &nid);
}

void MainWindow::RemoveTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = m_hwnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void MainWindow::ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, 1, L"Show Window");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, 2, m_isRunning ? L"Stop Server" : L"Start Server");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, 3, L"Exit");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
    } else if (cmd == 2) {
        PostMessage(m_hwnd, WM_COMMAND, IDC_BTN_STARTSTOP, 0);
    } else if (cmd == 3) {
        PostQuitMessage(0);
    }
}

void MainWindow::RefreshSourceList() {
    SendMessage(m_hListSources, LB_RESETCONTENT, 0, 0);
    for (const auto& src : AppConfig.mediaSources) {
        SendMessageW(m_hListSources, LB_ADDSTRING, 0, (LPARAM)src.path.c_str());
    }
}

void MainWindow::OpenFolderPicker() {
    IFileOpenDialog *pFileOpen;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
        }
        hr = pFileOpen->Show(m_hwnd);
        if (SUCCEEDED(hr)) {
            IShellItem *pItem;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    AppConfig.mediaSources.push_back({pszFilePath, true});
                    AppConfig.Save();
                    RefreshSourceList();
                    AppMedia.Scan();
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = NULL;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (MainWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, uMsg, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        // Toolbar Area (0 to 48)
        RECT rcToolbar = { 0, 0, rcClient.right, 48 };
        FillRect(hdc, &rcToolbar, m_hDarkBrush);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 220));
        
        HFONT hTitleFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ hOldFont = SelectObject(hdc, hTitleFont);
        
        RECT rcTitle = { 15, 10, 300, 48 };
        DrawTextW(hdc, L"dlna-server", -1, &rcTitle, DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);
        DeleteObject(hTitleFont);

        // Status Area (48 to 72)
        RECT rcStatus = { 0, 48, rcClient.right, 72 };
        FillRect(hdc, &rcStatus, m_hBgBrush);

        HFONT hStatusFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hOldFont = SelectObject(hdc, hStatusFont);

        RECT rcStatusText = { 15, 48, rcClient.right, 72 };
        std::wstring statusText = m_isRunning ? L"dlna-server is running on " + m_statusEndpoint : L"dlna-server is stopped";
        DrawTextW(hdc, statusText.c_str(), -1, &rcStatusText, DT_SINGLELINE | DT_VCENTER);

        // Subtitle if empty
        if (SendMessage(m_hListSources, LB_GETCOUNT, 0, 0) == 0) {
            RECT rcSubtitle = { 15, 80, rcClient.right, 100 };
            SetTextColor(hdc, RGB(150, 150, 150));
            DrawTextW(hdc, L"Please add shared folders or files (button \"+\")", -1, &rcSubtitle, DT_SINGLELINE | DT_TOP);
        }

        SelectObject(hdc, hOldFont);
        DeleteObject(hStatusFont);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        // Move buttons to the right
        SetWindowPos(m_hBtnAdd, NULL, width - 130, 10, 30, 30, SWP_NOZORDER);
        SetWindowPos(m_hBtnStartStop, NULL, width - 90, 10, 30, 30, SWP_NOZORDER);
        SetWindowPos(m_hBtnSettings, NULL, width - 50, 10, 30, 30, SWP_NOZORDER);

        // Resize list
        SetWindowPos(m_hListSources, NULL, 0, 72, width, height - 72, SWP_NOZORDER);

        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case IDC_BTN_STARTSTOP:
            if (m_isRunning) {
                DLNAServer.Stop();
                SetStatus(false);
            } else {
                if (DLNAServer.Start()) {
                    SetStatus(true, DLNAServer.GetEndpoint());
                }
            }
            break;
        case IDC_BTN_ADD:
            OpenFolderPicker();
            break;
        case IDC_BTN_SETTINGS:
        {
            int oldPort = AppConfig.port;
            INT_PTR result = SettingsDialog::Show(hwnd);
            if (result == IDOK && m_isRunning && AppConfig.port != oldPort) {
                DLNAServer.Stop();
                SetStatus(false);
                if (DLNAServer.Start()) {
                    SetStatus(true, DLNAServer.GetEndpoint());
                } else {
                    MessageBoxW(hwnd, L"Server stopped. Failed to restart on the new port.", L"Restart failed", MB_ICONWARNING | MB_OK);
                }
            }
            break;
        }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(220, 220, 220));
        SetBkColor(hdcStatic, RGB(30, 30, 30));
        return (INT_PTR)m_hBgBrush;
    }
    case WM_DRAWITEM: {
        // TODO: custom listbox draw
        return TRUE;
    }
    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu();
        } else if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
        }
        return 0;
    }
    case WM_CLOSE: {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    case WM_DESTROY: {
        DLNAServer.Stop();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
