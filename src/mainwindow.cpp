#include "mainwindow.h"
#include "../resources/resource.h"
#include "settingsdlg.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <cwctype>
#include "config.h"
#include "media_sources.h"
#include "server.h"

#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ID 1

namespace {
const int kGutter = 24;
const int kToolbarHeight = 56;
const int kStatusHeight = 40;
const int kButtonSize = 40;
const int kButtonGap = 8;
const int kListTop = kToolbarHeight + kStatusHeight + 8;
const int kCornerDiameter = 8;
const int kSourcePromptWidth = 520;
const int kSourcePromptHeight = 190;
const int IDC_SOURCE_EDIT = 4101;
const int IDC_SOURCE_BROWSE_FOLDER = 4102;
const int IDC_SOURCE_BROWSE_PLAYLIST = 4103;
const int IDC_SOURCE_ADD = 4104;
const int IDC_SOURCE_CANCEL = 4105;

COLORREF kPageColor = RGB(32, 32, 32);
COLORREF kToolbarColor = RGB(40, 40, 40);
COLORREF kControlColor = RGB(45, 45, 45);
COLORREF kControlPressedColor = RGB(58, 58, 58);
COLORREF kBorderColor = RGB(72, 72, 72);
COLORREF kTextColor = RGB(244, 244, 244);
COLORREF kSecondaryTextColor = RGB(190, 190, 190);

std::wstring TrimWideInput(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && iswspace(value[start])) ++start;
    size_t end = value.size();
    while (end > start && iswspace(value[end - 1])) --end;
    return value.substr(start, end - start);
}

std::wstring BrowseFolder(HWND owner) {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    std::wstring result;
    if (SUCCEEDED(hr)) {
        DWORD dwOptions = 0;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
        }
        if (SUCCEEDED(pFileOpen->Show(owner))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    result = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return result;
}

std::wstring BrowsePlaylist(HWND owner) {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    std::wstring result;
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Playlist files", L"*.m3u;*.m3u8;*.pls" },
            { L"All files", L"*.*" },
        };
        pFileOpen->SetFileTypes(2, filters);
        pFileOpen->SetTitle(L"Choose playlist file");
        if (SUCCEEDED(pFileOpen->Show(owner))) {
            IShellItem* pItem = nullptr;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    result = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return result;
}

struct SourcePromptState {
    HWND owner = NULL;
    HWND edit = NULL;
    bool done = false;
    bool accepted = false;
    std::wstring value;
};

void FinishSourcePrompt(HWND hwnd, SourcePromptState* state, bool accepted) {
    if (accepted) {
        int length = GetWindowTextLengthW(state->edit);
        std::wstring text(length + 1, L'\0');
        GetWindowTextW(state->edit, text.data(), length + 1);
        text.resize(length);
        state->value = TrimWideInput(text);
        state->accepted = !state->value.empty();
    }
    state->done = true;
    DestroyWindow(hwnd);
}

LRESULT CALLBACK SourcePromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SourcePromptState* state = reinterpret_cast<SourcePromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<SourcePromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HWND label = CreateWindowW(L"STATIC", L"Add a folder, playlist file, or network share URL:",
            WS_VISIBLE | WS_CHILD, 16, 16, 470, 20, hwnd, NULL, NULL, NULL);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            16, 44, 470, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_EDIT)), NULL, NULL);
        HWND hint = CreateWindowW(L"STATIC", L"Example: smb://user:pass@server/share or ftp://user:pass@server:21/media",
            WS_VISIBLE | WS_CHILD, 16, 74, 470, 20, hwnd, NULL, NULL, NULL);
        HWND folder = CreateWindowW(L"BUTTON", L"Folder...",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, 16, 108, 96, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_BROWSE_FOLDER)), NULL, NULL);
        HWND playlist = CreateWindowW(L"BUTTON", L"Playlist...",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, 120, 108, 96, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_BROWSE_PLAYLIST)), NULL, NULL);
        HWND add = CreateWindowW(L"BUTTON", L"Add",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 326, 108, 76, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_ADD)), NULL, NULL);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, 410, 108, 76, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_CANCEL)), NULL, NULL);
        HWND controls[] = { label, state->edit, hint, folder, playlist, add, cancel };
        for (HWND control : controls) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_SOURCE_BROWSE_FOLDER) {
            std::wstring selected = BrowseFolder(hwnd);
            if (!selected.empty()) SetWindowTextW(state->edit, selected.c_str());
            return 0;
        }
        if (id == IDC_SOURCE_BROWSE_PLAYLIST) {
            std::wstring selected = BrowsePlaylist(hwnd);
            if (!selected.empty()) SetWindowTextW(state->edit, selected.c_str());
            return 0;
        }
        if (id == IDC_SOURCE_ADD) {
            FinishSourcePrompt(hwnd, state, true);
            return 0;
        }
        if (id == IDC_SOURCE_CANCEL) {
            FinishSourcePrompt(hwnd, state, false);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        FinishSourcePrompt(hwnd, state, false);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::wstring PromptForMediaSource(HWND owner, HINSTANCE instance) {
    const wchar_t* className = L"dlna-server_SourcePrompt";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SourcePromptProc;
        wc.hInstance = instance;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    SourcePromptState state;
    state.owner = owner;
    RECT ownerRect = {};
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - kSourcePromptWidth) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - kSourcePromptHeight) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"Add media source",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, kSourcePromptWidth, kSourcePromptHeight,
        owner, NULL, instance, &state);
    if (!hwnd) return L"";

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (!state.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.accepted ? state.value : L"";
}
}

MainWindow::MainWindow() : m_hwnd(NULL), m_hInstance(NULL), m_isRunning(false),
m_hBtnAdd(NULL), m_hBtnStartStop(NULL), m_hBtnSettings(NULL), m_hListSources(NULL) {
    m_hBgBrush = CreateSolidBrush(kPageColor);
    m_hDarkBrush = CreateSolidBrush(kControlColor);
    m_hToolbarBrush = CreateSolidBrush(kToolbarColor);
    m_hTitleFont = NULL;
    m_hBodyFont = NULL;
    m_hIconFont = NULL;
}

MainWindow::~MainWindow() {
    RemoveTrayIcon();
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hDarkBrush) DeleteObject(m_hDarkBrush);
    if (m_hToolbarBrush) DeleteObject(m_hToolbarBrush);
    if (m_hTitleFont) DeleteObject(m_hTitleFont);
    if (m_hBodyFont) DeleteObject(m_hBodyFont);
    if (m_hIconFont) DeleteObject(m_hIconFont);
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
        0, CLASS_NAME, L"DLNA Server",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, this
    );

    if (m_hwnd == NULL) return false;

    m_hTitleFont = CreateUiFont(20, FW_SEMIBOLD, L"Segoe UI Variable");
    m_hBodyFont = CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable");
    m_hIconFont = CreateUiFont(16, FW_NORMAL, L"Segoe MDL2 Assets");

    m_hBtnAdd = CreateWindowExW(0, L"BUTTON", L"\xE710",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kButtonSize, kButtonSize, m_hwnd, (HMENU)IDC_BTN_ADD, hInstance, NULL);

    m_hBtnStartStop = CreateWindowExW(0, L"BUTTON", L"\xE768",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kButtonSize, kButtonSize, m_hwnd, (HMENU)IDC_BTN_STARTSTOP, hInstance, NULL);

    m_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"\xE713",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kButtonSize, kButtonSize, m_hwnd, (HMENU)IDC_BTN_SETTINGS, hInstance, NULL);

    if(m_hIconFont) {
        SendMessage(m_hBtnAdd, WM_SETFONT, (WPARAM)m_hIconFont, TRUE);
        SendMessage(m_hBtnStartStop, WM_SETFONT, (WPARAM)m_hIconFont, TRUE);
        SendMessage(m_hBtnSettings, WM_SETFONT, (WPARAM)m_hIconFont, TRUE);
    }

    m_hListSources = CreateWindowExW(0, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        kGutter, kListTop, 800 - (kGutter * 2), 600 - kListTop - kGutter,
        m_hwnd, (HMENU)IDC_LIST_SOURCES, hInstance, NULL);
    if (m_hBodyFont) {
        SendMessage(m_hListSources, WM_SETFONT, (WPARAM)m_hBodyFont, TRUE);
    }

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
    SendMessage(m_hBtnStartStop, WM_SETTEXT, 0, (LPARAM)(running ? L"\xE71A" : L"\xE768"));
    InvalidateRect(m_hwnd, NULL, TRUE);
}

HFONT MainWindow::CreateUiFont(int pixelSize, int weight, const wchar_t* faceName) {
    HDC hdc = GetDC(m_hwnd);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) {
        ReleaseDC(m_hwnd, hdc);
    }

    int height = -MulDiv(pixelSize, dpiY, 96);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, faceName);
}

void MainWindow::AddTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = m_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    wcscpy_s(nid.szTip, L"DLNA Server");

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

void MainWindow::DrawToolbarButton(const DRAWITEMSTRUCT* drawItem) {
    RECT rc = drawItem->rcItem;
    bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;

    COLORREF fillColor = pressed ? kControlPressedColor : kControlColor;
    COLORREF textColor = disabled ? RGB(128, 128, 128) : kTextColor;
    HBRUSH fillBrush = CreateSolidBrush(fillColor);
    HPEN borderPen = CreatePen(PS_SOLID, 1, kBorderColor);
    HGDIOBJ oldBrush = SelectObject(drawItem->hDC, fillBrush);
    HGDIOBJ oldPen = SelectObject(drawItem->hDC, borderPen);

    SetBkMode(drawItem->hDC, TRANSPARENT);
    RoundRect(drawItem->hDC, rc.left, rc.top, rc.right, rc.bottom, kCornerDiameter, kCornerDiameter);

    wchar_t text[8] = {};
    GetWindowTextW(drawItem->hwndItem, text, 8);
    HFONT oldFont = (HFONT)SelectObject(drawItem->hDC, m_hIconFont ? m_hIconFont : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(drawItem->hDC, textColor);
    DrawTextW(drawItem->hDC, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    if (drawItem->itemState & ODS_FOCUS) {
        RECT focus = rc;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(drawItem->hDC, &focus);
    }

    SelectObject(drawItem->hDC, oldFont);
    SelectObject(drawItem->hDC, oldPen);
    SelectObject(drawItem->hDC, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
}

void MainWindow::OpenFolderPicker() {
    std::wstring selected = PromptForMediaSource(m_hwnd, m_hInstance);
    if (selected.empty()) return;

    for (const auto& source : AppConfig.mediaSources) {
        if (source.path == selected) return;
    }

    AppConfig.mediaSources.push_back({selected, true});
    AppConfig.Save();
    RefreshSourceList();
    AppMedia.Scan();
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

        RECT rcToolbar = { 0, 0, rcClient.right, kToolbarHeight };
        FillRect(hdc, &rcToolbar, m_hToolbarBrush);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kTextColor);
        
        HGDIOBJ hOldFont = SelectObject(hdc, m_hTitleFont ? m_hTitleFont : GetStockObject(DEFAULT_GUI_FONT));
        
        int titleRight = rcClient.right - 160;
        if (titleRight < kGutter) {
            titleRight = kGutter;
        }
        RECT rcTitle = { kGutter, 0, titleRight, kToolbarHeight };
        DrawTextW(hdc, L"DLNA Server", -1, &rcTitle, DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, hOldFont);

        RECT rcStatus = { 0, kToolbarHeight, rcClient.right, kToolbarHeight + kStatusHeight };
        FillRect(hdc, &rcStatus, m_hBgBrush);

        hOldFont = SelectObject(hdc, m_hBodyFont ? m_hBodyFont : GetStockObject(DEFAULT_GUI_FONT));

        RECT rcStatusText = { kGutter, kToolbarHeight, rcClient.right - kGutter, kToolbarHeight + kStatusHeight };
        std::wstring statusText = m_isRunning ? L"DLNA Server is running on " + m_statusEndpoint : L"DLNA Server is stopped";
        DrawTextW(hdc, statusText.c_str(), -1, &rcStatusText, DT_SINGLELINE | DT_VCENTER);

        if (SendMessage(m_hListSources, LB_GETCOUNT, 0, 0) == 0) {
            RECT rcSubtitle = { kGutter + 16, kListTop + 16, rcClient.right - kGutter - 16, kListTop + 40 };
            SetTextColor(hdc, kSecondaryTextColor);
            DrawTextW(hdc, L"Please add shared folders or files (button \"+\")", -1, &rcSubtitle, DT_SINGLELINE | DT_TOP);
        }

        SelectObject(hdc, hOldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        int buttonTop = 8;
        int settingsLeft = width - kGutter - kButtonSize;
        int startLeft = settingsLeft - kButtonGap - kButtonSize;
        int addLeft = startLeft - kButtonGap - kButtonSize;
        SetWindowPos(m_hBtnAdd, NULL, addLeft, buttonTop, kButtonSize, kButtonSize, SWP_NOZORDER);
        SetWindowPos(m_hBtnStartStop, NULL, startLeft, buttonTop, kButtonSize, kButtonSize, SWP_NOZORDER);
        SetWindowPos(m_hBtnSettings, NULL, settingsLeft, buttonTop, kButtonSize, kButtonSize, SWP_NOZORDER);

        int listWidth = width - (kGutter * 2);
        int listHeight = height - kListTop - kGutter;
        if (listWidth < 0) {
            listWidth = 0;
        }
        if (listHeight < 0) {
            listHeight = 0;
        }
        SetWindowPos(m_hListSources, NULL, kGutter, kListTop, listWidth, listHeight, SWP_NOZORDER);

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
        SetTextColor(hdcStatic, kTextColor);
        SetBkColor(hdcStatic, kPageColor);
        return (INT_PTR)m_hBgBrush;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* drawItem = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (drawItem && drawItem->CtlType == ODT_BUTTON) {
            DrawToolbarButton(drawItem);
            return TRUE;
        }
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
