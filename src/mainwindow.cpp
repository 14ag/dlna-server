#include "mainwindow.h"
#include "../resources/resource.h"
#include "settingsdlg.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <cwctype>
#include <thread>
#include <atomic>
#include "config.h"
#include "media_sources.h"
#include "modal_focus.h"
#include "server.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define WM_TRAYICON (WM_USER + 1)
#define WM_SCAN_DONE (WM_USER + 2)
#define WM_SERVER_OPERATION_DONE (WM_APP + 10)
#define WM_SERVER_OPERATION_PROGRESS (WM_APP + 11)
#define TRAY_ID 1

namespace {
const int kGutter = 16;
const int kToolbarHeight = 56;
const int kStatusHeight = 40;
const int kButtonHeight = 32;
const int kButtonGap = 8;
const int kListTop = kToolbarHeight + kStatusHeight + 8;
const int kCornerDiameter = 8;
const int kDefaultWindowWidth = 440;
const int kDefaultWindowHeight = 600;
const int kAddButtonWidth = 56;
const int kDeleteButtonWidth = 72;
const int kStartStopButtonWidth = 72;
const int kSettingsButtonWidth = 82;
const int kSourcePromptWidth = 552;
const int kSourcePromptHeight = 216;
const int kSourcePromptContentWidth = kSourcePromptWidth - (kGutter * 2);
const int kSourcePromptLabelHeight = 20;
const int kSourcePromptEditTop = kGutter + kSourcePromptLabelHeight + 12;
const int kSourcePromptHintTop = kSourcePromptEditTop + kButtonHeight + kButtonGap;
const int kSourcePromptButtonTop = kSourcePromptHintTop + kSourcePromptLabelHeight + 20;
const int IDC_SOURCE_EDIT = 4101;
const int IDC_SOURCE_BROWSE_FOLDER = 4102;
const int IDC_SOURCE_BROWSE_PLAYLIST = 4103;
const int IDC_SOURCE_ADD = 4104;
const int IDC_SOURCE_CANCEL = 4105;

COLORREF kPageColor = RGB(31, 31, 31);
COLORREF kToolbarColor = RGB(37, 37, 37);
COLORREF kControlColor = RGB(51, 51, 51);
COLORREF kControlHoverColor = RGB(62, 62, 62);
COLORREF kControlPressedColor = RGB(74, 74, 74);
COLORREF kBorderColor = RGB(88, 88, 88);
COLORREF kFocusColor = RGB(96, 165, 250);
COLORREF kTextColor = RGB(255, 255, 255);
COLORREF kDisabledTextColor = RGB(132, 132, 132);
COLORREF kSecondaryTextColor = RGB(200, 200, 200);

HFONT CreateScaledFont(HWND hwnd, int pixelSize, int weight, const wchar_t* faceName) {
    HDC hdc = GetDC(hwnd);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) {
        ReleaseDC(hwnd, hdc);
    }

    return CreateFontW(-MulDiv(pixelSize, dpiY, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, faceName);
}

HFONT SourcePromptFont(HWND hwnd) {
    static HFONT font = CreateScaledFont(hwnd, 14, FW_NORMAL, L"Segoe UI Variable Text");
    return font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void ApplyDarkFrame(HWND hwnd) {
    BOOL darkFrame = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
}

struct ServerOperationResult {
    ServerUiState finalState;
    bool success;
    std::wstring endpoint;
    std::wstring message;
};

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
    ModalFocusSnapshot focusSnapshot;
    bool done = false;
    bool accepted = false;
    std::wstring value;
};

void FinishSourcePrompt(HWND hwnd, SourcePromptState* state, bool accepted) {
    if (accepted) {
        int length = GetWindowTextLengthW(state->edit);
        std::wstring text(length + 1, L'\0');
        GetWindowTextW(state->edit, &text[0], length + 1);
        text.resize(length);
        state->value = TrimWideInput(text);
        state->accepted = !state->value.empty();
    }
    state->done = true;
    EnableOwnerAndRestoreModalFocus(state->focusSnapshot, state->owner);
    DestroyWindow(hwnd);
}

LRESULT CALLBACK SourcePromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SourcePromptState* state = reinterpret_cast<SourcePromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<SourcePromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        ApplyDarkFrame(hwnd);

        HFONT font = SourcePromptFont(hwnd);
        HWND label = CreateWindowW(L"STATIC", L"Add a folder, playlist file, or network share URL:",
            WS_VISIBLE | WS_CHILD, kGutter, kGutter, kSourcePromptContentWidth, kSourcePromptLabelHeight, hwnd, NULL, NULL, NULL);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            kGutter, kSourcePromptEditTop, kSourcePromptContentWidth, kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_EDIT)), NULL, NULL);
        HWND hint = CreateWindowW(L"STATIC", L"Example: ftp://user:pass@server:21/media",
            WS_VISIBLE | WS_CHILD, kGutter, kSourcePromptHintTop, kSourcePromptContentWidth, kSourcePromptLabelHeight, hwnd, NULL, NULL, NULL);
        HWND folder = CreateWindowW(L"BUTTON", L"Folder...",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, kGutter, kSourcePromptButtonTop, 96, kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_BROWSE_FOLDER)), NULL, NULL);
        HWND playlist = CreateWindowW(L"BUTTON", L"Playlist...",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, kGutter + 96 + kButtonGap, kSourcePromptButtonTop, 96, kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_BROWSE_PLAYLIST)), NULL, NULL);
        HWND add = CreateWindowW(L"BUTTON", L"Add",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, kSourcePromptWidth - kGutter - 78 - kButtonGap - 78, kSourcePromptButtonTop, 78, kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_ADD)), NULL, NULL);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel",
            WS_VISIBLE | WS_CHILD | WS_TABSTOP, kSourcePromptWidth - kGutter - 78, kSourcePromptButtonTop, 78, kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_CANCEL)), NULL, NULL);
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
            RestoreModalFocus(state->focusSnapshot, state->edit);
            return 0;
        }
        if (id == IDC_SOURCE_BROWSE_PLAYLIST) {
            std::wstring selected = BrowsePlaylist(hwnd);
            if (!selected.empty()) SetWindowTextW(state->edit, selected.c_str());
            RestoreModalFocus(state->focusSnapshot, state->edit);
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
    state.focusSnapshot = CaptureModalFocus(owner);
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

    MSG msg = {};
    BOOL getResult = 0;
    while (!state.done && (getResult = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (!state.done) {
        EnableOwnerAndRestoreModalFocus(state.focusSnapshot, owner);
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
    }
    if (getResult == 0) {
        PostQuitMessage(static_cast<int>(msg.wParam));
    }
    return state.accepted ? state.value : L"";
}
}

MainWindow::MainWindow() : m_hwnd(NULL), m_hInstance(NULL), m_state(ServerUiState::Stopped),
m_hBtnAdd(NULL), m_hBtnDelete(NULL), m_hBtnStartStop(NULL), m_hBtnSettings(NULL), m_hListSources(NULL), m_listOldProc(NULL),
m_startedHeadless(false), m_scanInProgress(false), m_scanningStatusActive(false), m_pendingRescanAfterBusy(false), m_lastSelectedIndex(-1) {
    m_hBgBrush = CreateSolidBrush(kPageColor);
    m_hDarkBrush = CreateSolidBrush(kControlColor);
    m_hToolbarBrush = CreateSolidBrush(kToolbarColor);
    m_hTitleFont = NULL;
    m_hBodyFont = NULL;
    m_hButtonFont = NULL;
}

MainWindow::~MainWindow() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
    DLNAServer.Stop();
    SetThreadExecutionState(ES_CONTINUOUS);
    RemoveTrayIcon();
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
    if (m_hDarkBrush) DeleteObject(m_hDarkBrush);
    if (m_hToolbarBrush) DeleteObject(m_hToolbarBrush);
    if (m_hTitleFont) DeleteObject(m_hTitleFont);
    if (m_hBodyFont) DeleteObject(m_hBodyFont);
    if (m_hButtonFont) DeleteObject(m_hButtonFont);
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow, bool startHeadless) {
    m_hInstance = hInstance;
    m_startedHeadless = startHeadless;

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
        m_startedHeadless ? WS_EX_TOOLWINDOW : 0, CLASS_NAME, L"DLNA Server",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWindowWidth, kDefaultWindowHeight,
        NULL, NULL, hInstance, this
    );

    if (m_hwnd == NULL) return false;
    ApplyDarkFrame(m_hwnd);

    m_hTitleFont = CreateUiFont(20, FW_SEMIBOLD, L"Segoe UI Variable Display");
    m_hBodyFont = CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable Text");
    m_hButtonFont = CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable Text");

    m_hBtnAdd = CreateWindowExW(0, L"BUTTON", L"Add",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kAddButtonWidth, kButtonHeight, m_hwnd, (HMENU)IDC_BTN_ADD, hInstance, NULL);

    m_hBtnDelete = CreateWindowExW(0, L"BUTTON", L"Delete",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kDeleteButtonWidth, kButtonHeight, m_hwnd, (HMENU)IDC_BTN_DELETE, hInstance, NULL);

    m_hBtnStartStop = CreateWindowExW(0, L"BUTTON", L"Start",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kStartStopButtonWidth, kButtonHeight, m_hwnd, (HMENU)IDC_BTN_STARTSTOP, hInstance, NULL);

    m_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"Settings",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, kSettingsButtonWidth, kButtonHeight, m_hwnd, (HMENU)IDC_BTN_SETTINGS, hInstance, NULL);

    if(m_hButtonFont) {
        SendMessage(m_hBtnAdd, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
        SendMessage(m_hBtnDelete, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
        SendMessage(m_hBtnStartStop, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
        SendMessage(m_hBtnSettings, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
    }

    m_hListSources = CreateWindowExW(0, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        kGutter, kListTop, kDefaultWindowWidth - (kGutter * 2), kDefaultWindowHeight - kListTop - kGutter,
        m_hwnd, (HMENU)IDC_LIST_SOURCES, hInstance, NULL);
    if (m_hBodyFont) {
        SendMessage(m_hListSources, WM_SETFONT, (WPARAM)m_hBodyFont, TRUE);
    }
    SetWindowLongPtrW(m_hListSources, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_listOldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hListSources, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListBoxProc)));

    RefreshSourceList();
    UpdateDeleteButton();

    AddTrayIcon();

    if (nCmdShow != SW_HIDE) {
        ShowWindow(m_hwnd, nCmdShow);
    }
    return true;
}

void MainWindow::SetStatus(ServerUiState state, const std::wstring& endpoint) {
    m_state = state;
    m_statusEndpoint = endpoint;
    SendMessage(m_hBtnStartStop, WM_SETTEXT, 0, (LPARAM)(IsRunning() ? L"Stop" : L"Start"));
    SetControlsForState();
    UpdateWakeLock();
    InvalidateRect(m_hwnd, NULL, TRUE);
}

bool MainWindow::IsBusy() const {
    return m_state == ServerUiState::Starting || m_state == ServerUiState::Stopping;
}

bool MainWindow::IsRunning() const {
    return m_state == ServerUiState::Running;
}

void MainWindow::UpdateWakeLock() {
    if (m_state == ServerUiState::Stopped) {
        SetThreadExecutionState(ES_CONTINUOUS);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    }
}

void MainWindow::SetControlsForState() {
    BOOL busyOrScanning = (IsBusy() || m_scanInProgress.load()) ? FALSE : TRUE;
    EnableWindow(m_hBtnAdd, busyOrScanning);
    EnableWindow(m_hBtnStartStop, busyOrScanning);
    EnableWindow(m_hBtnSettings, TRUE);
    SendMessage(m_hBtnAdd, WM_SETTEXT, 0, (LPARAM)(IsRunning() ? L"Scan" : L"Add"));
    UpdateDeleteButton();
}

void MainWindow::BeginStartServer() {
    if (IsBusy() || IsRunning()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Starting);
    HWND target = m_hwnd;
    m_worker = std::thread([target]() {
        bool ok = DLNAServer.Start();
        ServerOperationResult* result = new ServerOperationResult{
            ok ? ServerUiState::Running : ServerUiState::Stopped,
            ok,
            ok ? DLNAServer.GetEndpoint() : L"",
            ok ? L"" : L"Failed to start DLNA server."
        };
        PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
    });
}

void MainWindow::BeginStopServer() {
    if (IsBusy() || !IsRunning()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Stopping, m_statusEndpoint);
    HWND target = m_hwnd;
    m_worker = std::thread([target]() {
        DLNAServer.Stop();
        ServerOperationResult* result = new ServerOperationResult{ ServerUiState::Stopped, true, L"", L"" };
        PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
    });
}

void MainWindow::BeginRestartServer() {
    if (IsBusy()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Stopping, m_statusEndpoint);
    HWND target = m_hwnd;
    m_worker = std::thread([target]() {
        DLNAServer.Stop();
        PostMessageW(target, WM_SERVER_OPERATION_PROGRESS, static_cast<WPARAM>(ServerUiState::Starting), 0);
        bool ok = DLNAServer.Start();
        ServerOperationResult* result = new ServerOperationResult{
            ok ? ServerUiState::Running : ServerUiState::Stopped,
            ok,
            ok ? DLNAServer.GetEndpoint() : L"",
            ok ? L"" : L"Server stopped. Failed to restart on the new port."
        };
        PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
    });
}

void MainWindow::CompleteServerOperation(ServerUiState finalState, const std::wstring& endpoint, bool success, const std::wstring& message) {
    if (m_worker.joinable()) {
        m_worker.join();
    }
    SetStatus(finalState, endpoint);
    if (!success && !message.empty()) {
        MessageBoxW(m_hwnd, message.c_str(), L"DLNA Server", MB_ICONWARNING | MB_OK);
    }
    if (m_pendingRescanAfterBusy.exchange(false)) {
        std::thread([]() { DLNAServer.Rescan(); }).detach();
    }
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
    AppendMenuW(hMenu, MF_STRING, 1, L"Show Window");
    AppendMenuW(hMenu, MF_STRING | (IsBusy() ? MF_GRAYED : 0), 2, IsRunning() ? L"Stop Server" : L"Start Server");
    AppendMenuW(hMenu, MF_STRING, 3, L"Exit");

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        RestoreAndFocusMainWindow();
    } else if (cmd == 2 && !IsBusy()) {
        PostMessage(m_hwnd, WM_COMMAND, IDC_BTN_STARTSTOP, 0);
    } else if (cmd == 3) {
        PostQuitMessage(0);
    }
}

void MainWindow::RestoreAndFocusMainWindow() {
    if (m_startedHeadless) {
        LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_TOOLWINDOW) {
            exStyle &= ~WS_EX_TOOLWINDOW;
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);
            SetWindowPos(m_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
            m_startedHeadless = false;
        }
    }
    ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void MainWindow::RefreshSourceList() {
    SendMessage(m_hListSources, LB_RESETCONTENT, 0, 0);
    for (const auto& src : AppConfig.mediaSources) {
        SendMessageW(m_hListSources, LB_ADDSTRING, 0, (LPARAM)src.path.c_str());
    }
    UpdateDeleteButton();
}

int MainWindow::SelectedSourceIndex() const {
    if (!m_hListSources) return -1;
    LRESULT selected = SendMessage(m_hListSources, LB_GETCURSEL, 0, 0);
    return selected == LB_ERR ? -1 : static_cast<int>(selected);
}

void MainWindow::UpdateDeleteButton() {
    if (!m_hBtnDelete) return;
    int idx = SelectedSourceIndex();
    if (idx < 0) idx = m_lastSelectedIndex;
    EnableWindow(m_hBtnDelete, idx >= 0 ? TRUE : FALSE);
}

void MainWindow::RemoveSelectedSource() {
    int selected = SelectedSourceIndex();
    if (selected < 0) selected = m_lastSelectedIndex;
    m_lastSelectedIndex = -1;
    if (selected < 0 || selected >= static_cast<int>(AppConfig.mediaSources.size())) {
        UpdateDeleteButton();
        return;
    }

    AppConfig.Mutate([selected](Config& cfg) {
        cfg.mediaSources.erase(cfg.mediaSources.begin() + selected);
    });
    AppConfig.Save();
    RefreshSourceList();

    int count = static_cast<int>(SendMessage(m_hListSources, LB_GETCOUNT, 0, 0));
    if (count > 0) {
        int nextSelection = selected < count ? selected : count - 1;
        SendMessage(m_hListSources, LB_SETCURSEL, nextSelection, 0);
    }
    UpdateDeleteButton();
    InvalidateRect(m_hwnd, NULL, TRUE);

    if (IsBusy()) {
        m_pendingRescanAfterBusy.store(true);
    } else {
        std::thread([]() { DLNAServer.Rescan(); }).detach();
    }
}

void MainWindow::DrawToolbarButton(const DRAWITEMSTRUCT* drawItem) {
    RECT rc = drawItem->rcItem;
    bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    bool hot = (drawItem->itemState & ODS_HOTLIGHT) != 0;

    COLORREF fillColor = pressed ? kControlPressedColor : (hot ? kControlHoverColor : kControlColor);
    COLORREF textColor = disabled ? kDisabledTextColor : kTextColor;
    HBRUSH fillBrush = CreateSolidBrush(fillColor);
    HPEN borderPen = CreatePen(PS_SOLID, 1, kBorderColor);
    HGDIOBJ oldBrush = SelectObject(drawItem->hDC, fillBrush);
    HGDIOBJ oldPen = SelectObject(drawItem->hDC, borderPen);

    SetBkMode(drawItem->hDC, TRANSPARENT);
    RoundRect(drawItem->hDC, rc.left, rc.top, rc.right, rc.bottom, kCornerDiameter, kCornerDiameter);

    wchar_t text[32] = {};
    GetWindowTextW(drawItem->hwndItem, text, 32);
    HFONT oldFont = (HFONT)SelectObject(drawItem->hDC, m_hButtonFont ? m_hButtonFont : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(drawItem->hDC, textColor);
    DrawTextW(drawItem->hDC, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);

    if (drawItem->itemState & ODS_FOCUS) {
        RECT focus = rc;
        InflateRect(&focus, -4, -4);
        HPEN focusPen = CreatePen(PS_SOLID, 1, kFocusColor);
        HGDIOBJ oldFocusPen = SelectObject(drawItem->hDC, focusPen);
        HGDIOBJ oldFocusBrush = SelectObject(drawItem->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(drawItem->hDC, focus.left, focus.top, focus.right, focus.bottom);
        SelectObject(drawItem->hDC, oldFocusBrush);
        SelectObject(drawItem->hDC, oldFocusPen);
        DeleteObject(focusPen);
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

    bool alreadyPresent = false;
    AppConfig.Mutate([&selected, &alreadyPresent](Config& cfg) {
        for (const auto& source : cfg.mediaSources) {
            if (source.path == selected) {
                alreadyPresent = true;
                return;
            }
        }
        cfg.mediaSources.push_back({selected, true});
    });
    if (alreadyPresent) return;
    AppConfig.Save();
    RefreshSourceList();
    std::thread([]() { DLNAServer.Rescan(); }).detach();
}

void MainWindow::BeginRescan() {
    if (IsBusy()) return;
    if (m_scanInProgress.exchange(true)) return;
    m_scanningStatusActive = true;
    SetControlsForState();
    InvalidateRect(m_hwnd, NULL, TRUE);
    HWND target = m_hwnd;
    std::thread([target]() {
        DLNAServer.Rescan();
        PostMessageW(target, WM_SCAN_DONE, 0, 0);
    }).detach();
}

LRESULT CALLBACK MainWindow::ListBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (pThis && uMsg == WM_KILLFOCUS) {
        SendMessage(hwnd, LB_SETCURSEL, (WPARAM)-1, 0);
        pThis->UpdateDeleteButton();
    }
    if (pThis && uMsg == WM_KEYDOWN && wParam == VK_DELETE) {
        pThis->RemoveSelectedSource();
        return 0;
    }
    if (pThis && pThis->m_listOldProc) {
        return CallWindowProcW(pThis->m_listOldProc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
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
        
        int titleRight = rcClient.right - (kAddButtonWidth + kDeleteButtonWidth + kStartStopButtonWidth + kSettingsButtonWidth + kButtonGap * 4 + kGutter);
        if (titleRight < kGutter) {
            titleRight = kGutter;
        }
        RECT rcTitle = { kGutter, 0, titleRight, kToolbarHeight };
        SelectObject(hdc, hOldFont);

        RECT rcStatus = { 0, kToolbarHeight, rcClient.right, kToolbarHeight + kStatusHeight };
        FillRect(hdc, &rcStatus, m_hBgBrush);

        hOldFont = SelectObject(hdc, m_hBodyFont ? m_hBodyFont : GetStockObject(DEFAULT_GUI_FONT));

        RECT rcStatusText = { kGutter, kToolbarHeight, rcClient.right - kGutter, kToolbarHeight + kStatusHeight };
        std::wstring statusText;
        if (m_scanningStatusActive) {
            statusText = L"scanning...";
        } else if (m_state == ServerUiState::Starting) {
            statusText = L"starting server...";
        } else if (m_state == ServerUiState::Stopping) {
            statusText = L"stopping server...";
        } else if (m_state == ServerUiState::Running) {
            statusText = L"Server running";
        }
        DrawTextW(hdc, statusText.c_str(), -1, &rcStatusText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        if (SendMessage(m_hListSources, LB_GETCOUNT, 0, 0) == 0) {
            RECT rcSubtitle = { kGutter + 16, kListTop + 16, rcClient.right - kGutter - 16, kListTop + 40 };
            SetTextColor(hdc, kSecondaryTextColor);
            DrawTextW(hdc, L"Please add shared folders or files with Add.", -1, &rcSubtitle, DT_SINGLELINE | DT_TOP);
        }

        SelectObject(hdc, hOldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        int buttonTop = (kToolbarHeight - kButtonHeight) / 2;
        int settingsLeft = width - kGutter - kSettingsButtonWidth;
        int startLeft = settingsLeft - kButtonGap - kStartStopButtonWidth;
        int deleteLeft = startLeft - kButtonGap - kDeleteButtonWidth;
        int addLeft = deleteLeft - kButtonGap - kAddButtonWidth;
        SetWindowPos(m_hBtnAdd, NULL, addLeft, buttonTop, kAddButtonWidth, kButtonHeight, SWP_NOZORDER);
        SetWindowPos(m_hBtnDelete, NULL, deleteLeft, buttonTop, kDeleteButtonWidth, kButtonHeight, SWP_NOZORDER);
        SetWindowPos(m_hBtnStartStop, NULL, startLeft, buttonTop, kStartStopButtonWidth, kButtonHeight, SWP_NOZORDER);
        SetWindowPos(m_hBtnSettings, NULL, settingsLeft, buttonTop, kSettingsButtonWidth, kButtonHeight, SWP_NOZORDER);

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
            if (IsRunning()) {
                BeginStopServer();
            } else {
                BeginStartServer();
            }
            break;
        case IDC_BTN_ADD:
            if (IsRunning()) {
                BeginRescan();
            } else {
                OpenFolderPicker();
            }
            break;
        case IDC_BTN_DELETE:
            RemoveSelectedSource();
            break;
        case IDC_BTN_SETTINGS:
        {
            int oldPort = AppConfig.port;
            INT_PTR result = SettingsDialog::Show(hwnd);
            if (IsRunning() && (result == IDC_BTN_RESTART || (result == IDOK && AppConfig.port != oldPort))) {
                BeginRestartServer();
            }
            break;
        }
        }
        if (wmId == IDC_LIST_SOURCES && HIWORD(wParam) == LBN_SELCHANGE) {
            m_lastSelectedIndex = SelectedSourceIndex();
            UpdateDeleteButton();
        }
        return 0;
    }
    case WM_SCAN_DONE: {
        m_scanInProgress.store(false);
        m_scanningStatusActive = false;
        SetControlsForState();
        InvalidateRect(m_hwnd, NULL, TRUE);
        return 0;
    }
    case WM_SERVER_OPERATION_PROGRESS: {
        SetStatus(static_cast<ServerUiState>(wParam), m_statusEndpoint);
        return 0;
    }
    case WM_SERVER_OPERATION_DONE: {
        ServerOperationResult* result = reinterpret_cast<ServerOperationResult*>(lParam);
        if (result) {
            CompleteServerOperation(result->finalState, result->endpoint, result->success, result->message);
            delete result;
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
        if (lParam == WM_LBUTTONUP) {
            RestoreAndFocusMainWindow();
        } else if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu();
        } else if (lParam == WM_LBUTTONDBLCLK) {
            RestoreAndFocusMainWindow();
        }
        return 0;
    }
    case WM_CLOSE: {
        if (DLNAServer.IsRunning()) {
            ShowWindow(hwnd, SW_HIDE);
        } else {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_DESTROY: {
        if (m_worker.joinable()) {
            m_worker.join();
        }
        DLNAServer.Stop();
        SetThreadExecutionState(ES_CONTINUOUS);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
