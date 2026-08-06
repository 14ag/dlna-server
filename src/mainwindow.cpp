#include "mainwindow.h"
#include "../resources/resource.h"
#include "settingsdlg.h"
#include "media_source_file_types.h"
#include "server_close_policy.h"
#include "ui_font.h"
#include "dark_frame.h"
#include "win_geometry_dump.h"
#include "ui_tokens.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <powrprof.h>
#include <uxtheme.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string>
#include <cwctype>
#include <thread>
#include <atomic>
#include "config.h"
#include "dlna_utils.h"
#include "media_sources.h"
#include "thread_guard.h"
#include "input_gate.h"
#include "modal_focus.h"
#include "function_key_action.h"
#include "help_dialog.h"
#include "server.h"
#include "ssdp.h"
#include "log.h"
#include "tray_notify.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "powrprof.lib")

#define WM_TRAYICON (WM_USER + 1)
#define WM_SCAN_DONE (WM_USER + 2)
#define WM_SERVER_OPERATION_DONE (WM_APP + 10)
#define WM_SERVER_OPERATION_PROGRESS (WM_APP + 11)
#define TRAY_ID 1

namespace {
const int kListTop = UiTokens::kToolbarHeight + UiTokens::kStatusHeight + UiTokens::kListTopGap;
const int kSourcePromptWidth = 552;
const int kSourcePromptContentWidth = kSourcePromptWidth - (UiTokens::kGutter * 2);
const int kSourcePromptLabelHeight = 20;
const int kSourcePromptEditTop = UiTokens::kGutter + kSourcePromptLabelHeight + 12;
const int kSourcePromptHintTop = kSourcePromptEditTop + UiTokens::kButtonHeight + UiTokens::kButtonGap;
const int kSourcePromptButtonTop = kSourcePromptHintTop + kSourcePromptLabelHeight + 20;
const int kSourcePromptBottomMargin = UiTokens::kGutter;
const int kDialogChromeAllowance = 40;
const int kSourcePromptHeight = kSourcePromptButtonTop + UiTokens::kButtonHeight + kSourcePromptBottomMargin + kDialogChromeAllowance;
const int IDC_SOURCE_EDIT = 4101;
const int IDC_SOURCE_BROWSE_FOLDER = 4102;
const int IDC_SOURCE_BROWSE_FILE = 4106;
const int IDC_SOURCE_ADD = 4104;
const int IDC_SOURCE_CANCEL = 4105;

HFONT SourcePromptFont(HWND hwnd) {
    static HFONT font = CreateScaledFont(hwnd, 14, FW_NORMAL, L"Segoe UI Variable Text");
    return font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

struct ServerOperationResult {
    ServerUiState finalState;
    bool success;
    std::wstring endpoint;
    std::wstring message;
};

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

std::wstring BuildMediaSourceFilterPattern() {
    // built from the single shared extension list in
    // media_source_file_types.h (media + playlist extensions combined)
    // instead of a separately hand-typed string literal, so this can
    // never drift out of sync with the POSIX build
    std::wstring pattern;
    for (const auto& ext : GetMediaSourceFileExtensions()) {
        if (!pattern.empty()) pattern += L";";
        pattern += L"*." + ext;
    }
    return pattern;
}

std::wstring BrowseMediaFile(HWND owner) {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    std::wstring result;
    if (SUCCEEDED(hr)) {
        const std::wstring pattern = BuildMediaSourceFilterPattern();
        COMDLG_FILTERSPEC filters[] = {
            { L"Media & playlist files", pattern.c_str() },
            { L"All files", L"*.*" },
        };
        pFileOpen->SetFileTypes(2, filters);
        pFileOpen->SetTitle(L"Choose media or playlist file");
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
        state->value = TrimWide(text);
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
    HWND label = CreateWindowW(L"STATIC", L"Add a local source or a Network share URL:",
        WS_VISIBLE | WS_CHILD, UiTokens::kGutter, UiTokens::kGutter, kSourcePromptContentWidth, kSourcePromptLabelHeight, hwnd, NULL, NULL, NULL);
    state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_GROUP | ES_AUTOHSCROLL,
        UiTokens::kGutter, kSourcePromptEditTop, kSourcePromptContentWidth, UiTokens::kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_EDIT)), NULL, NULL);
    HWND hint = CreateWindowW(L"STATIC", L"Example: ftp://user:pass@server:21/media",
        WS_VISIBLE | WS_CHILD, UiTokens::kGutter, kSourcePromptHintTop, kSourcePromptContentWidth, kSourcePromptLabelHeight, hwnd, NULL, NULL, NULL);
    // assign mnemonics for all four buttons in one call so duplicate letters resolve correctly
    std::vector<std::wstring> srcLabels = { L"Folder...", L"File...", L"Add", L"Cancel" };
    std::vector<wchar_t> srcMnemonics = AssignMnemonics(srcLabels);
    HWND folder = CreateWindowW(L"BUTTON", InsertMnemonicMarker(srcLabels[0], srcMnemonics[0]).c_str(),
        WS_VISIBLE | WS_CHILD | WS_TABSTOP, UiTokens::kGutter, kSourcePromptButtonTop, 96, UiTokens::kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_BROWSE_FOLDER)), NULL, NULL);
    // FEAT-01: File... now covers both media and playlist extensions
    // (see BuildMediaSourceFilterPattern above), so it is widened to
    // occupy the horizontal space the removed Playlist... button and
    // its gap used to take: 96 (old Playlist width) + UiTokens::kButtonGap +
    // 96 (old File width) = 96 + 8 + 96 = 200. Using 200 here (not 204)
    // keeps the button's right edge exactly where the old File...
    // button's right edge was, so nothing to its right needs to move.
    HWND file = CreateWindowW(L"BUTTON", InsertMnemonicMarker(srcLabels[1], srcMnemonics[1]).c_str(),
        WS_VISIBLE | WS_CHILD | WS_TABSTOP, UiTokens::kGutter + 96 + UiTokens::kButtonGap, kSourcePromptButtonTop, 200, UiTokens::kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_BROWSE_FILE)), NULL, NULL);
    HWND add = CreateWindowW(L"BUTTON", InsertMnemonicMarker(srcLabels[2], srcMnemonics[2]).c_str(),
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, kSourcePromptWidth - UiTokens::kGutter - 78 - UiTokens::kButtonGap - 78, kSourcePromptButtonTop, 78, UiTokens::kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_ADD)), NULL, NULL);
    HWND cancel = CreateWindowW(L"BUTTON", InsertMnemonicMarker(srcLabels[3], srcMnemonics[3]).c_str(),
        WS_VISIBLE | WS_CHILD | WS_TABSTOP, kSourcePromptWidth - UiTokens::kGutter - 78, kSourcePromptButtonTop, 78, UiTokens::kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_CANCEL)), NULL, NULL);
    HWND controls[] = { label, state->edit, hint, folder, file, add, cancel };
        for (HWND control : controls) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_SOURCE_ADD), FALSE);
        SetFocus(state->edit);
        DumpDialogGeometry(hwnd, L"source-prompt-geometry");
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_SOURCE_EDIT && HIWORD(wParam) == EN_CHANGE) {
            EnableWindow(GetDlgItem(hwnd, IDC_SOURCE_ADD),
                         AnyFieldHasContent({ GetWindowTextLengthW(state->edit) }) ? TRUE : FALSE);
            return 0;
        }
        if (id == IDC_SOURCE_BROWSE_FOLDER) {
            std::wstring selected = BrowseFolder(hwnd);
            if (!selected.empty()) SetWindowTextW(state->edit, selected.c_str());
            RestoreModalFocus(state->focusSnapshot, state->edit);
            return 0;
        }
        if (id == IDC_SOURCE_BROWSE_FILE) {
            std::wstring selected = BrowseMediaFile(hwnd);
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
        if (msg.message == WM_KEYDOWN && msg.hwnd != hwnd) {
            if (msg.wParam == VK_ESCAPE) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                continue;
            }
            if (msg.wParam == VK_BACK) {
                wchar_t ctrlClassName[32] = {};
                GetClassNameW(msg.hwnd, ctrlClassName, 32);
                bool isLiveEdit = _wcsicmp(ctrlClassName, L"EDIT") == 0 &&
                                   !(GetWindowLongW(msg.hwnd, GWL_STYLE) & ES_READONLY);
                if (!isLiveEdit) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    continue;
                }
            }
        }
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
m_hBtnAdd(NULL), m_hBtnDelete(NULL), m_hBtnStartStop(NULL), m_hBtnSettings(NULL), m_hListSources(NULL), m_listOldProc(NULL), m_toolbarOldProc(NULL),
m_startedHeadless(false), m_scanInProgress(false), m_scanningStatusActive(false) {
    m_hBgBrush = CreateSolidBrush(RGB(UiTokens::kPageColor.r, UiTokens::kPageColor.g, UiTokens::kPageColor.b));
    m_hDarkBrush = CreateSolidBrush(RGB(UiTokens::kControlColor.r, UiTokens::kControlColor.g, UiTokens::kControlColor.b));
    m_hToolbarBrush = CreateSolidBrush(RGB(UiTokens::kToolbarColor.r, UiTokens::kToolbarColor.g, UiTokens::kToolbarColor.b));
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
    if (m_hListSources) {
        RevokeDragDrop(m_hListSources);
    }
    if (m_sourceDropTarget) {
        m_sourceDropTarget->Release();
        m_sourceDropTarget = nullptr;
    }
    OleUninitialize();
    if (m_hSuspendResumeNotify) {
        PowerUnregisterSuspendResumeNotification(m_hSuspendResumeNotify);
        m_hSuspendResumeNotify = NULL;
    }
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

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    // LoadIconMetric provides DPI-aware icon sizing (Vista+).
    // Fall back to LoadIcon if the metric API is unavailable.
    if (FAILED(LoadIconMetric(hInstance, MAKEINTRESOURCE(IDI_APP_ICON),
                              LIM_LARGE, &wc.hIcon))) {
        wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    }
    if (FAILED(LoadIconMetric(hInstance, MAKEINTRESOURCE(IDI_APP_ICON),
                              LIM_SMALL, &wc.hIconSm))) {
        wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    }
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = m_hBgBrush;

    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        m_startedHeadless ? WS_EX_TOOLWINDOW : 0, CLASS_NAME, L"DLNA Server",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, UiTokens::kWindowWidth, UiTokens::kWindowHeight,
        NULL, NULL, hInstance, this
    );

    if (m_hwnd == NULL) return false;
    ApplyDarkFrame(m_hwnd);

    m_hTitleFont = CreateUiFont(20, FW_SEMIBOLD, L"Segoe UI Variable Display");
    m_hBodyFont = CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable Text");
    m_hButtonFont = CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable Text");

    // button row is one navigation group and the source list is a second group
    // WS_GROUP on the first control of each group is what IsDialogMessage uses
    // to know where one group ends and the next begins
    m_hBtnAdd = CreateWindowExW(0, L"BUTTON", L"Add",
        WS_TABSTOP | WS_GROUP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, UiTokens::kAddButtonWidth, UiTokens::kButtonHeight, m_hwnd, (HMENU)IDC_BTN_ADD, hInstance, NULL);

    m_hBtnDelete = CreateWindowExW(0, L"BUTTON", L"Delete",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, UiTokens::kDeleteButtonWidth, UiTokens::kButtonHeight, m_hwnd, (HMENU)IDC_BTN_DELETE, hInstance, NULL);

    m_hBtnStartStop = CreateWindowExW(0, L"BUTTON", L"Start",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, UiTokens::kStartStopButtonWidth, UiTokens::kButtonHeight, m_hwnd, (HMENU)IDC_BTN_STARTSTOP, hInstance, NULL);

    m_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"Settings",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
        0, 0, UiTokens::kSettingsButtonWidth, UiTokens::kButtonHeight, m_hwnd, (HMENU)IDC_BTN_SETTINGS, hInstance, NULL);

    if(m_hButtonFont) {
        SendMessage(m_hBtnAdd, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
        SendMessage(m_hBtnDelete, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
        SendMessage(m_hBtnStartStop, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
        SendMessage(m_hBtnSettings, WM_SETFONT, (WPARAM)m_hButtonFont, TRUE);
    }

    // subclass all four toolbar buttons to swallow up and down arrow
    // left and right arrow still reach default proc for IsDialogMessage
    // group navigation on them
    m_toolbarOldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hBtnAdd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ToolbarButtonProc)));
    SetWindowLongPtrW(m_hBtnAdd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(m_hBtnDelete, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ToolbarButtonProc));
    SetWindowLongPtrW(m_hBtnDelete, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(m_hBtnStartStop, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ToolbarButtonProc));
    SetWindowLongPtrW(m_hBtnStartStop, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(m_hBtnSettings, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ToolbarButtonProc));
    SetWindowLongPtrW(m_hBtnSettings, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_hListSources = CreateWindowExW(0, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | WS_VSCROLL | WS_BORDER |
        LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        UiTokens::kGutter, kListTop, UiTokens::kWindowWidth - (UiTokens::kGutter * 2), UiTokens::kWindowHeight - kListTop - UiTokens::kGutter,
        m_hwnd, (HMENU)IDC_LIST_SOURCES, hInstance, NULL);
    if (m_hBodyFont) {
        SendMessage(m_hListSources, WM_SETFONT, (WPARAM)m_hBodyFont, TRUE);
    }
    SetWindowLongPtrW(m_hListSources, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_listOldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m_hListSources, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListBoxProc)));
    // Strip theming so the theme engine cannot apply a DWM-level focus border
    // on the listbox NC frame (which would appear as blue lines on focus).
    SetWindowTheme(m_hListSources, L"", L"");

    OleInitialize(NULL);
    m_sourceDropTarget = new SourceListDropTarget(
        [this]() { return IsBusy() || IsRunning(); },
        [this](const std::vector<std::wstring>& paths) { HandleDroppedPaths(paths); });
    RegisterDragDrop(m_hListSources, m_sourceDropTarget);

    UpdateListLayout(UiTokens::kWindowWidth, UiTokens::kWindowHeight);

    RefreshSourceList();
    UpdateDeleteButton();

    AddTrayIcon();
    SetTimer(m_hwnd, kInitialScanPollTimerId, 250, NULL);

    PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_WINDOW_HANDLE, m_hwnd, &m_hSuspendResumeNotify);

    if (nCmdShow != SW_HIDE) {
        ShowWindow(m_hwnd, nCmdShow);
    }
    RefreshToolbarMnemonics();
    return true;
}

static std::wstring GetWindowTextAsWide(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(len);
    return text;
}

void MainWindow::RefreshToolbarMnemonics() {
    std::vector<std::wstring> labels = {
        GetWindowTextAsWide(m_hBtnAdd),
        GetWindowTextAsWide(m_hBtnDelete),
        GetWindowTextAsWide(m_hBtnStartStop),
        GetWindowTextAsWide(m_hBtnSettings)
    };
    std::vector<std::wstring> stripped;
    for (auto& l : labels) stripped.push_back(StripMnemonicMarker(l));
    std::vector<wchar_t> assigned = AssignMnemonics(stripped);
    m_lastMnemonics = assigned;
    SetWindowTextW(m_hBtnAdd, InsertMnemonicMarker(stripped[0], assigned[0]).c_str());
    SetWindowTextW(m_hBtnDelete, InsertMnemonicMarker(stripped[1], assigned[1]).c_str());
    SetWindowTextW(m_hBtnStartStop, InsertMnemonicMarker(stripped[2], assigned[2]).c_str());
    SetWindowTextW(m_hBtnSettings, InsertMnemonicMarker(stripped[3], assigned[3]).c_str());
}

bool MainWindow::TryHandleAccessKeyChar(wchar_t ch) {
    if (m_cueState.HideAccel()) return false;

    // Gate this on focus not being an editable text field: check GetFocus() is one of 
    // the known non-edit controls before treating the char as an access key, so typing 
    // inside any future edit control on the main window is never hijacked.
    HWND focused = GetFocus();
    if (focused != m_hListSources && focused != m_hBtnAdd && focused != m_hBtnDelete && 
        focused != m_hBtnStartStop && focused != m_hBtnSettings) {
        return false;
    }

    if (m_lastMnemonics.size() < 4) return false;
    
    wchar_t upCh = static_cast<wchar_t>(towupper(ch));
    if (upCh == L'\0') return false;

    if (upCh == static_cast<wchar_t>(towupper(m_lastMnemonics[0])) && IsWindowEnabled(m_hBtnAdd)) {
        SendMessageW(m_hwnd, WM_COMMAND, IDC_BTN_ADD, 0);
        return true;
    }
    if (upCh == static_cast<wchar_t>(towupper(m_lastMnemonics[1])) && IsWindowEnabled(m_hBtnDelete)) {
        SendMessageW(m_hwnd, WM_COMMAND, IDC_BTN_DELETE, 0);
        return true;
    }
    if (upCh == static_cast<wchar_t>(towupper(m_lastMnemonics[2])) && IsWindowEnabled(m_hBtnStartStop)) {
        SendMessageW(m_hwnd, WM_COMMAND, IDC_BTN_STARTSTOP, 0);
        return true;
    }
    if (upCh == static_cast<wchar_t>(towupper(m_lastMnemonics[3])) && IsWindowEnabled(m_hBtnSettings)) {
        SendMessageW(m_hwnd, WM_COMMAND, IDC_BTN_SETTINGS, 0);
        return true;
    }
    return false;
}

bool MainWindow::TryHandleFunctionKey(WPARAM vkCode) {
    FunctionKeyAction action = DecideFunctionKeyAction(
        static_cast<int>(vkCode), IsRunning(), IsBusy(), m_scanInProgress.load());
    switch (action) {
    case FunctionKeyAction::ShowHelp:
        HelpDialog::Show(m_hwnd);
        return true;
    case FunctionKeyAction::Rescan:
        BeginRescan();
        return true;
    case FunctionKeyAction::RefreshSourceList:
        RefreshSourceList();
        return true;
    case FunctionKeyAction::ShowSourceListContextMenu:
        // handled separately by wm contextmenu see task a4
        return false;
    case FunctionKeyAction::None:
        return false;
    }
    return false;
}

void MainWindow::SetStatus(ServerUiState state, const std::wstring& endpoint) {
    m_state = state;
    m_statusEndpoint = endpoint;
    SendMessage(m_hBtnStartStop, WM_SETTEXT, 0, (LPARAM)(IsRunning() ? L"Stop" : L"Start"));
    SetControlsForState();
    UpdateWakeLock();
    RefreshToolbarMnemonics();
    RefreshSourceList();
    InvalidateRect(m_hwnd, NULL, TRUE);
}

bool MainWindow::IsBusy() const {
    return m_state == ServerUiState::Starting || m_state == ServerUiState::Stopping;
}

bool MainWindow::IsRunning() const {
    return m_state == ServerUiState::Running;
}

bool MainWindow::IsShowingOverrideSources() const {
    // An override is only "being served" once the server that would serve
    // it is actually running. If an override was set on a stopped instance
    // (see the WM_COPYDATA not-running branch in Phase 3) the listbox must
    // keep showing config.ini's list until Start() actually picks the
    // override up -- otherwise the list and the Add/Delete affordances
    // would desync from what a subsequent manual Start() would do.
    return AppConfig.HasRuntimeSourceOverride() && IsRunning();
}

void MainWindow::UpdateWakeLock() {
    if (m_state == ServerUiState::Stopped) {
        SetThreadExecutionState(ES_CONTINUOUS);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    }
}

void MainWindow::SetControlsForState() {
    // start stop only waits on its own transition starting or stopping
    // a scan in progress must not block stop
    BOOL enableStartStop = IsBusy() ? FALSE : TRUE;
    // add slash scan additionally waits on m_scanInProgress
    // this prevents a second overlapping scan request
    BOOL enableAdd = (IsBusy() || m_scanInProgress.load() || DLNAServer.IsInitialScanInProgress()) ? FALSE : TRUE;
    EnableWindow(m_hBtnStartStop, enableStartStop);
    EnableWindow(m_hBtnAdd, enableAdd);
    EnableWindow(m_hBtnSettings, TRUE);
    SendMessage(m_hBtnAdd, WM_SETTEXT, 0, (LPARAM)(IsRunning() ? L"Scan" : L"Add"));
    RefreshToolbarMnemonics();
    UpdateDeleteButton();
}

void MainWindow::BeginStartServer() {
    if (IsBusy() || IsRunning()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Starting);
    HWND target = m_hwnd;
    m_worker = std::thread([target]() {
        RunGuarded(L"start-server-worker", [target]() {
            std::wstring reason;
            bool ok = DLNAServer.Start(reason);
            std::wstring message;
            if (!ok) {
                message = L"server could not start\n";
                if (!reason.empty()) message += reason;
            }
            ServerOperationResult* result = new ServerOperationResult{
                ok ? ServerUiState::Running : ServerUiState::Stopped,
                ok,
                ok ? DLNAServer.GetEndpoint() : L"",
                ok ? L"" : message.c_str()
            };
            PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
        });
    });
}

void MainWindow::BeginStopServer() {
    if (IsBusy() || !IsRunning()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Stopping, m_statusEndpoint);
    HWND target = m_hwnd;
    m_worker = std::thread([target]() {
        RunGuarded(L"stop-server-worker", [target]() {
            DLNAServer.Stop();
            ServerOperationResult* result = new ServerOperationResult{ ServerUiState::Stopped, true, L"", L"" };
            PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
        });
    });
}

void MainWindow::BeginRestartServer() {
    if (IsBusy()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Stopping, m_statusEndpoint);
    HWND target = m_hwnd;
    m_worker = std::thread([target]() {
        RunGuarded(L"restart-server-worker", [target]() {
            DLNAServer.Stop();
            PostMessageW(target, WM_SERVER_OPERATION_PROGRESS, static_cast<WPARAM>(ServerUiState::Starting), 0);
            std::wstring reason;
            bool ok = DLNAServer.Start(reason);
            std::wstring message;
            if (!ok) {
                message = L"server could not start\n";
                if (!reason.empty()) message += reason;
            }
            ServerOperationResult* result = new ServerOperationResult{
                ok ? ServerUiState::Running : ServerUiState::Stopped,
                ok,
                ok ? DLNAServer.GetEndpoint() : L"",
                ok ? L"" : message.c_str()
            };
            PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
        });
    });
}

void MainWindow::BeginSourceOverrideRestart(std::vector<MediaSource> overrideSources) {
    if (IsBusy()) return;
    if (m_worker.joinable()) m_worker.join();
    SetStatus(ServerUiState::Stopping, m_statusEndpoint);
    HWND target = m_hwnd;
    m_worker = std::thread([target, overrideSources = std::move(overrideSources)]() {
        RunGuarded(L"source-override-restart-worker", [target, overrideSources = std::move(overrideSources)]() mutable {
            DLNAServer.Stop();
            // Set the NEW override only after Stop() has fully run, since
            // Stop() unconditionally clears whatever override was active
            // (Phase 2). Setting it before Stop() would have it wiped.
            AppConfig.SetRuntimeSourceOverride(overrideSources);
            PostMessageW(target, WM_SERVER_OPERATION_PROGRESS, static_cast<WPARAM>(ServerUiState::Starting), 0);
            std::wstring reason;
            bool ok = DLNAServer.Start(reason);
            std::wstring message;
            if (!ok) {
                message = L"server could not start\n";
                if (!reason.empty()) message += reason;
            }
            ServerOperationResult* result = new ServerOperationResult{
                ok ? ServerUiState::Running : ServerUiState::Stopped,
                ok,
                ok ? DLNAServer.GetEndpoint() : L"",
                ok ? L"" : message
            };
            PostMessageW(target, WM_SERVER_OPERATION_DONE, 0, reinterpret_cast<LPARAM>(result));
        });
    });
}

void MainWindow::CompleteServerOperation(ServerUiState finalState, const std::wstring& endpoint, bool success, const std::wstring& message) {
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (finalState == ServerUiState::Running) {
        // prime the timer's change-detection so it always fires once the initial
        // scan finishes, even when the scan ends before the first timer tick that
        // would have seen it as in-progress
        m_lastPolledScanInProgress = true;
    }
    SetStatus(finalState, endpoint);
    if (!success && !message.empty()) {
        MessageBoxW(m_hwnd, message.c_str(), L"DLNA Server", MB_ICONWARNING | MB_OK);
    }
    if (m_closePending.ShouldCloseNowAfterOperation(finalState == ServerUiState::Stopped)) {
        DestroyWindow(m_hwnd);
        return;
    }
    if (m_closePending.ShouldStopAgainAfterOperation(finalState == ServerUiState::Running)) {
        BeginStopServer();
    }
}

HFONT MainWindow::CreateUiFont(int pixelSize, int weight, const wchar_t* faceName) {
    return CreateScaledFont(m_hwnd, pixelSize, weight, faceName);
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

    // NIM_SETVERSION with NOTIFYICON_VERSION_4 enables modern notification
    // area behavior (balloon replies, NIN_SELECT, etc.) on Vista+.
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
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

    std::vector<std::wstring> labels = {
        L"Show Window",
        IsRunning() ? L"Stop Server" : L"Start Server",
        L"Exit"
    };
    std::vector<wchar_t> assigned = AssignMnemonics(labels);

    AppendMenuW(hMenu, MF_STRING, 1, InsertMnemonicMarker(labels[0], assigned[0]).c_str());
    AppendMenuW(hMenu, MF_STRING | (IsBusy() ? MF_GRAYED : 0), 2, InsertMnemonicMarker(labels[1], assigned[1]).c_str());
    AppendMenuW(hMenu, MF_STRING, 3, InsertMnemonicMarker(labels[2], assigned[2]).c_str());

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

void MainWindow::ShowSourceListContextMenu(HWND sourceHwnd, int screenX, int screenY) {
    if (sourceHwnd != m_hListSources) return;
    if (screenX == -1 && screenY == -1) {
        RECT itemRect = {};
        int selected = SelectedSourceIndex();
        if (selected >= 0) {
            SendMessage(m_hListSources, LB_GETITEMRECT, selected, reinterpret_cast<LPARAM>(&itemRect));
        } else {
            GetClientRect(m_hListSources, &itemRect);
        }
        POINT origin = { itemRect.left, itemRect.bottom };
        ClientToScreen(m_hListSources, &origin);
        screenX = origin.x;
        screenY = origin.y;
    }

    HMENU menu = CreatePopupMenu();
    const bool canRemove = !m_focusState.IsNoFocus() && !IsBusy() && !m_scanInProgress.load()
                            && !IsShowingOverrideSources();
    AppendMenuW(menu, MF_STRING | (canRemove ? 0 : MF_GRAYED), IDC_BTN_DELETE, L"Remove selected source");
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, screenX, screenY, 0, m_hwnd, NULL);
    DestroyMenu(menu);
    if (cmd == IDC_BTN_DELETE && canRemove) {
        RemoveSelectedSource();
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
    const std::vector<MediaSource> displayed = IsShowingOverrideSources()
        ? AppConfig.GetRuntimeSourceOverride()
        : AppConfig.mediaSources;
    for (const auto& src : displayed) {
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
    const bool hasSelection = !m_focusState.IsNoFocus();
    const bool deletionAllowed = hasSelection && !IsBusy() && !m_scanInProgress.load()
        && !IsShowingOverrideSources();
    EnableWindow(m_hBtnDelete, deletionAllowed ? TRUE : FALSE);
}

void MainWindow::UpdateListLayout(int width, int height) {
    const int ringSpace = UiTokens::kFocusRingThickness + UiTokens::kFocusRingGap;
    int listLeft = UiTokens::kGutter + ringSpace;
    int listTop = kListTop + ringSpace;
    int listWidth = width - (UiTokens::kGutter * 2) - (ringSpace * 2);
    int listHeight = height - kListTop - UiTokens::kGutter - (ringSpace * 2);
    if (listWidth < 0) listWidth = 0;
    if (listHeight < 0) listHeight = 0;

    SetWindowPos(m_hListSources, NULL, listLeft, listTop, listWidth, listHeight, SWP_NOZORDER);

    m_listRingRect.left = listLeft - UiTokens::kFocusRingGap;
    m_listRingRect.top = listTop - UiTokens::kFocusRingGap;
    m_listRingRect.right = listLeft + listWidth + UiTokens::kFocusRingGap;
    m_listRingRect.bottom = listTop + listHeight + UiTokens::kFocusRingGap;
}

void MainWindow::ArmMouseTracking(HWND hwnd) {
    auto it = m_mouseTracking.find(hwnd);
    if (it != m_mouseTracking.end() && it->second) return;
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    if (TrackMouseEvent(&tme)) {
        m_mouseTracking[hwnd] = true;
    }
}

void MainWindow::RepaintHighlightTransition(int before, int after) {
    if (before == after) return;
    auto repaintOne = [this](int id) {
        if (id == HoverFocusState::kNoControl) return;
        if (id == IDC_LIST_SOURCES) {
            // Rectangle draws the ring pen centered on m_listRingRect
            // GDI Rectangle excludes the bottom and right edges of the rect it is given
            // a pen wider than 1px therefore paints asymmetrically outside the nominal rect
            // erasing or repainting with that same unpadded rect can miss those pixels
            // inflate the redraw target so it is a strict superset of the ring pen footprint
            RECT ringRedrawRect = m_listRingRect;
            InflateRect(&ringRedrawRect, UiTokens::kFocusRingThickness, UiTokens::kFocusRingThickness);
            RedrawWindow(m_hwnd, &ringRedrawRect, NULL,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
        } else {
            InvalidateRect(GetDlgItem(m_hwnd, id), NULL, TRUE);
        }
    };
    repaintOne(before);
    repaintOne(after);
}

void MainWindow::UpdateControlHover(HWND hwnd, int controlId, bool entered) {
    if (!entered) m_mouseTracking[hwnd] = false;
    const int before = m_hoverFocusState.HighlightedControlId();
    if (entered) m_hoverFocusState.OnMouseEnter(controlId);
    else m_hoverFocusState.OnMouseLeave(controlId);
    RepaintHighlightTransition(before, m_hoverFocusState.HighlightedControlId());
}

void MainWindow::UpdateControlFocus(int controlId, bool gained) {
    const int before = m_hoverFocusState.HighlightedControlId();
    if (gained) m_hoverFocusState.OnFocusGained(controlId);
    else m_hoverFocusState.OnFocusLost(controlId);
    RepaintHighlightTransition(before, m_hoverFocusState.HighlightedControlId());
}

void MainWindow::RemoveSelectedSource() {
    if (IsBusy() || m_scanInProgress.load() || IsShowingOverrideSources()) {
        return;
    }

    int selected = SelectedSourceIndex();
    if (selected < 0 || selected >= static_cast<int>(AppConfig.mediaSources.size())) {
        UpdateDeleteButton();
        return;
    }

    AppConfig.Mutate([selected](Config& cfg) {
        cfg.mediaSources.erase(cfg.mediaSources.begin() + selected);
    });
    AppConfig.Save();
    RefreshSourceList();

    SendMessage(m_hListSources, LB_SETCURSEL, (WPARAM)-1, 0);
    m_focusState.OnSourceDeleted();
    UpdateDeleteButton();
    InvalidateRect(m_hwnd, NULL, TRUE);

    std::thread([]() { RunGuarded(L"remove-source-rescan", []() { DLNAServer.Rescan(); }); }).detach();
}

void MainWindow::DrawToolbarButton(const DRAWITEMSTRUCT* drawItem) {
    RECT rc = drawItem->rcItem;
    bool pressed = (drawItem->itemState & ODS_SELECTED) != 0;
    bool disabled = (drawItem->itemState & ODS_DISABLED) != 0;
    bool hot = (drawItem->itemState & ODS_HOTLIGHT) != 0;

    COLORREF fillColor = pressed ? RGB(UiTokens::kControlPressedColor.r, UiTokens::kControlPressedColor.g, UiTokens::kControlPressedColor.b) : (hot ? RGB(UiTokens::kControlHoverColor.r, UiTokens::kControlHoverColor.g, UiTokens::kControlHoverColor.b) : RGB(UiTokens::kControlColor.r, UiTokens::kControlColor.g, UiTokens::kControlColor.b));
    COLORREF textColor = disabled ? RGB(UiTokens::kDisabledTextColor.r, UiTokens::kDisabledTextColor.g, UiTokens::kDisabledTextColor.b) : RGB(UiTokens::kTextColor.r, UiTokens::kTextColor.g, UiTokens::kTextColor.b);
    HBRUSH fillBrush = CreateSolidBrush(fillColor);
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(UiTokens::kBorderColor.r, UiTokens::kBorderColor.g, UiTokens::kBorderColor.b));
    HGDIOBJ oldBrush = SelectObject(drawItem->hDC, fillBrush);
    HGDIOBJ oldPen = SelectObject(drawItem->hDC, borderPen);

    SetBkMode(drawItem->hDC, TRANSPARENT);
    RoundRect(drawItem->hDC, rc.left, rc.top, rc.right, rc.bottom, UiTokens::kCornerRadius, UiTokens::kCornerRadius);

    wchar_t text[32] = {};
    GetWindowTextW(drawItem->hwndItem, text, 32);
    HFONT oldFont = (HFONT)SelectObject(drawItem->hDC, m_hButtonFont ? m_hButtonFont : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(drawItem->hDC, textColor);
    UINT drawFlags = DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS;
    if (m_cueState.HideAccel()) drawFlags |= DT_HIDEPREFIX;
    DrawTextW(drawItem->hDC, text, -1, &rc, drawFlags);

    const int controlId = static_cast<int>(drawItem->CtlID);
    const bool isHovered = (m_hoverFocusState.HoveredControlId() == controlId);
    const bool isFocusedOnly = !isHovered && (m_hoverFocusState.FocusedControlId() == controlId);
    const bool showFocusRing = isHovered || (isFocusedOnly && !m_cueState.HideFocus());
    if (showFocusRing) {
        RECT focus = rc;
        InflateRect(&focus, -4, -4);
        HPEN focusPen = CreatePen(PS_SOLID, 1, RGB(UiTokens::kFocusColor.r, UiTokens::kFocusColor.g, UiTokens::kFocusColor.b));
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

bool MainWindow::AddMediaSourceIfNew(const std::wstring& path) {
    bool alreadyPresent = false;
    AppConfig.Mutate([&path, &alreadyPresent](Config& cfg) {
        for (const auto& source : cfg.mediaSources) {
            if (source.path == path) {
                alreadyPresent = true;
                return;
            }
        }
        cfg.mediaSources.push_back({path});
    });
    if (alreadyPresent) {
        return false;
    }
    AppConfig.Save();
    RefreshSourceList();
    std::thread([]() { RunGuarded(L"add-source-rescan", []() { DLNAServer.Rescan(); }); }).detach();
    return true;
}

void MainWindow::OpenFolderPicker() {
    std::wstring selected = PromptForMediaSource(m_hwnd, m_hInstance);
    if (selected.empty()) return;
    AddMediaSourceIfNew(selected);
}

void MainWindow::HandleDroppedPaths(const std::vector<std::wstring>& paths) {
    if (IsBusy() || IsRunning()) {
        return;
    }
    for (const auto& path : paths) {
        if (IsSupportedLocalMediaOrPlaylistPath(path)) {
            AddMediaSourceIfNew(path);
        }
    }
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

// swallow up slash down arrow on toolbar buttons only
// left slash right arrow still reach the default window proc so
// IsDialogMessage keeps handling group navigation for them unchanged
LRESULT CALLBACK MainWindow::ToolbarButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    const int controlId = GetDlgCtrlID(hwnd);
    if (pThis && uMsg == WM_MOUSEMOVE) {
        pThis->ArmMouseTracking(hwnd);
        pThis->UpdateControlHover(hwnd, controlId, true);
    }
    if (pThis && uMsg == WM_MOUSELEAVE) {
        pThis->UpdateControlHover(hwnd, controlId, false);
    }
    if (pThis && uMsg == WM_SETFOCUS) {
        pThis->UpdateControlFocus(controlId, true);
    }
    if (pThis && uMsg == WM_KILLFOCUS) {
        pThis->UpdateControlFocus(controlId, false);
    }
    if (uMsg == WM_KEYDOWN && (wParam == VK_UP || wParam == VK_DOWN)) {
        return 0;
    }
    if (pThis && pThis->m_toolbarOldProc) {
        return CallWindowProcW(pThis->m_toolbarOldProc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MainWindow::ListBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (uMsg == WM_NCPAINT) {
        // Suppress the system's blue focus border on the NC frame.
        // We draw our own ring in the parent's WM_PAINT; the system border
        // just draws over it and leaves ghost lines on focus change.
        HDC hdc = GetWindowDC(hwnd);
        if (hdc) {
            RECT rcWindow;
            GetWindowRect(hwnd, &rcWindow);
            OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
            HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(UiTokens::kBorderColor.r, UiTokens::kBorderColor.g, UiTokens::kBorderColor.b));
            HGDIOBJ oldPen = SelectObject(hdc, borderPen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, rcWindow.left, rcWindow.top, rcWindow.right, rcWindow.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(borderPen);
            ReleaseDC(hwnd, hdc);
        }
        return 0;
    }

    if (pThis && uMsg == WM_MOUSEMOVE) {
        // Check that the mouse is actually inside the listbox.
        // WM_MOUSEMOVE can fire with outside coords when the listbox
        // has mouse capture (from WM_LBUTTONDOWN), which would otherwise
        // keep the hover ring alive after the mouse leaves.
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        if (PtInRect(&rcClient, pt)) {
            pThis->ArmMouseTracking(hwnd);
            pThis->UpdateControlHover(hwnd, IDC_LIST_SOURCES, true);
        } else {
            pThis->UpdateControlHover(hwnd, IDC_LIST_SOURCES, false);
        }
    }
    if (pThis && uMsg == WM_MOUSELEAVE) {
        pThis->UpdateControlHover(hwnd, IDC_LIST_SOURCES, false);
    }
    if (pThis && uMsg == WM_KILLFOCUS) {
        const bool gainedFocusIsDeleteButton = reinterpret_cast<HWND>(wParam) == pThis->m_hBtnDelete;
        pThis->m_focusState.OnListBoxLostFocus(gainedFocusIsDeleteButton);
        if (!gainedFocusIsDeleteButton) {
            SendMessage(hwnd, LB_SETCURSEL, (WPARAM)-1, 0);
        }
        pThis->UpdateDeleteButton();
        // Note: listbox ring is hover-only; no focus tracking here.
    }
    if (pThis && uMsg == WM_KEYDOWN && wParam == 'D' && !pThis->m_focusState.IsNoFocus()) {
        pThis->RemoveSelectedSource();
        return 0;
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

        RECT rcToolbar = { 0, 0, rcClient.right, UiTokens::kToolbarHeight };
        FillRect(hdc, &rcToolbar, m_hToolbarBrush);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(UiTokens::kTextColor.r, UiTokens::kTextColor.g, UiTokens::kTextColor.b));
        
        HGDIOBJ hOldFont = SelectObject(hdc, m_hTitleFont ? m_hTitleFont : GetStockObject(DEFAULT_GUI_FONT));
        
        int titleRight = rcClient.right - (UiTokens::kAddButtonWidth + UiTokens::kDeleteButtonWidth + UiTokens::kStartStopButtonWidth + UiTokens::kSettingsButtonWidth + UiTokens::kButtonGap * 4 + UiTokens::kGutter);
        if (titleRight < UiTokens::kGutter) {
            titleRight = UiTokens::kGutter;
        }
        RECT rcTitle = { UiTokens::kGutter, 0, titleRight, UiTokens::kToolbarHeight };
        SelectObject(hdc, hOldFont);

        RECT rcStatus = { 0, UiTokens::kToolbarHeight, rcClient.right, UiTokens::kToolbarHeight + UiTokens::kStatusHeight };
        FillRect(hdc, &rcStatus, m_hBgBrush);

        hOldFont = SelectObject(hdc, m_hBodyFont ? m_hBodyFont : GetStockObject(DEFAULT_GUI_FONT));

        RECT rcStatusText = { UiTokens::kGutter, UiTokens::kToolbarHeight, rcClient.right - UiTokens::kGutter, UiTokens::kToolbarHeight + UiTokens::kStatusHeight };
        std::wstring statusText;
        if (m_scanningStatusActive || (IsRunning() && DLNAServer.IsInitialScanInProgress())) {
            statusText = L"scanning...";
        } else if (m_state == ServerUiState::Starting) {
            statusText = L"starting server...";
        } else if (m_state == ServerUiState::Stopping) {
            statusText = L"stopping server...";
        } else if (m_state == ServerUiState::Running) {
            statusText = AppConfig.HasRuntimeSourceOverride() ? L"temporary source" : L"Server running";
        }
        DrawTextW(hdc, statusText.c_str(), -1, &rcStatusText, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        if (SendMessage(m_hListSources, LB_GETCOUNT, 0, 0) == 0) {
            RECT rcSubtitle = { UiTokens::kGutter + 16, kListTop + 16, rcClient.right - UiTokens::kGutter - 16, kListTop + 40 };
            SetTextColor(hdc, RGB(UiTokens::kSecondaryTextColor.r, UiTokens::kSecondaryTextColor.g, UiTokens::kSecondaryTextColor.b));
            DrawTextW(hdc, L"Please add shared folders or files with Add.", -1, &rcSubtitle, DT_SINGLELINE | DT_TOP);
        }

        if (m_hoverFocusState.HighlightedControlId() == IDC_LIST_SOURCES) {
            HPEN ringPen = CreatePen(PS_SOLID, UiTokens::kFocusRingThickness, RGB(UiTokens::kFocusColor.r, UiTokens::kFocusColor.g, UiTokens::kFocusColor.b));
            HGDIOBJ oldRingPen = SelectObject(hdc, ringPen);
            HGDIOBJ oldRingBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, m_listRingRect.left, m_listRingRect.top, m_listRingRect.right, m_listRingRect.bottom);
            SelectObject(hdc, oldRingBrush);
            SelectObject(hdc, oldRingPen);
            DeleteObject(ringPen);
        }

        SelectObject(hdc, hOldFont);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        int buttonTop = (UiTokens::kToolbarHeight - UiTokens::kButtonHeight) / 2;
        int settingsLeft = width - UiTokens::kGutter - UiTokens::kSettingsButtonWidth;
        int startLeft = settingsLeft - UiTokens::kButtonGap - UiTokens::kStartStopButtonWidth;
        int deleteLeft = startLeft - UiTokens::kButtonGap - UiTokens::kDeleteButtonWidth;
        int addLeft = deleteLeft - UiTokens::kButtonGap - UiTokens::kAddButtonWidth;
        SetWindowPos(m_hBtnAdd, NULL, addLeft, buttonTop, UiTokens::kAddButtonWidth, UiTokens::kButtonHeight, SWP_NOZORDER);
        SetWindowPos(m_hBtnDelete, NULL, deleteLeft, buttonTop, UiTokens::kDeleteButtonWidth, UiTokens::kButtonHeight, SWP_NOZORDER);
        SetWindowPos(m_hBtnStartStop, NULL, startLeft, buttonTop, UiTokens::kStartStopButtonWidth, UiTokens::kButtonHeight, SWP_NOZORDER);
        SetWindowPos(m_hBtnSettings, NULL, settingsLeft, buttonTop, UiTokens::kSettingsButtonWidth, UiTokens::kButtonHeight, SWP_NOZORDER);

        UpdateListLayout(width, height);

        m_statusRect = { 0, UiTokens::kToolbarHeight, width, UiTokens::kToolbarHeight + UiTokens::kStatusHeight };

        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_ACTIVATEAPP: {
        if (!wParam) {
            // App losing focus: clear hover so the ring does not persist
            // when the user clicks outside the application window.
            const int before = m_hoverFocusState.HighlightedControlId();
            m_hoverFocusState.OnMouseLeave(m_hoverFocusState.HoveredControlId());
            for (auto& kv : m_mouseTracking) kv.second = false;
            RepaintHighlightTransition(before, m_hoverFocusState.HighlightedControlId());
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    case WM_UPDATEUISTATE: {
        WORD action = LOWORD(wParam);
        WORD flags = HIWORD(wParam);
        if (flags & UISF_HIDEACCEL) {
            if (action == UIS_SET) m_cueState.OnMouseButtonInput();
            if (action == UIS_CLEAR) m_cueState.OnKeyboardInput();
        }
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
            INT_PTR result = SettingsDialog::Show(hwnd);
            if (result == IDOK && IsRunning() && SettingsDialog::WasRestartRequested()) {
                BeginRestartServer();
            }
            break;
        }
        }
        if (wmId == IDC_LIST_SOURCES && HIWORD(wParam) == LBN_SELCHANGE) {
            m_focusState.OnSelectionChanged(SelectedSourceIndex() >= 0);
            UpdateDeleteButton();
        }
        return 0;
    }
    case WM_SCAN_DONE: {
        m_scanInProgress.store(false);
        m_scanningStatusActive = false;
        SetControlsForState();
        // move focus off the add slash scan button once the scan ends
        // so it does not keep a focus rectangle after re enabling
        if (GetFocus() == m_hBtnAdd) {
            SetFocus(m_hListSources);
        }
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
        SetTextColor(hdcStatic, RGB(UiTokens::kTextColor.r, UiTokens::kTextColor.g, UiTokens::kTextColor.b));
        SetBkColor(hdcStatic, RGB(UiTokens::kPageColor.r, UiTokens::kPageColor.g, UiTokens::kPageColor.b));
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
    case WM_CONTEXTMENU: {
        HWND sourceHwnd = reinterpret_cast<HWND>(wParam);
        int screenX = GET_X_LPARAM(lParam);
        int screenY = GET_Y_LPARAM(lParam);
        ShowSourceListContextMenu(sourceHwnd, screenX, screenY);
        return 0;
    }
    case WM_SHOW_EXISTING_INSTANCE: {
        RestoreAndFocusMainWindow();
        return 0;
    }
    case WM_REQUEST_SHUTDOWN: {
        // Mirrors WM_CLOSE exactly (see that case above). Previously this
        // called DestroyWindow() unconditionally, with no check against a
        // start/stop/restart worker being mid-flight. If one was, the
        // worker's later PostMessageW(WM_SERVER_OPERATION_DONE, ...)
        // targets a window that no longer exists by the time the message
        // loop would dispatch it, so its heap-allocated
        // ServerOperationResult is never delete'd. Routing through the
        // same ShouldCloseNow/ClosePendingState gate WM_CLOSE already
        // uses defers the actual destroy until CompleteServerOperation()
        // has processed that message and freed the result -- see
        // F-CRASH-03.
        if (ShouldCloseNow(DLNAServer.IsRunning(), IsBusy())) {
            DestroyWindow(hwnd);
        } else {
            if (m_state == ServerUiState::Stopping) {
                m_closePending.RequestCloseOnceStopped();
            }
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    }
    case WM_KILL_SERVER: {
        // --kill-server path.  Always performs a full graceful teardown.
        // Must not call DLNAServer.Stop() while a BeginStartServer/
        // BeginStopServer/BeginRestartServer worker is still mid-flight on
        // m_worker: Server::Start()'s reentrancy guard is a plain atomic
        // load (not compare-and-swap), so a concurrent Stop() here can
        // silently no-op while Start() is still running and later leaves
        // the server running despite this kill request -- see F-CMR-03.
        // Joining m_worker first is safe and cannot deadlock: this handler
        // runs on the UI thread, and every m_worker body only ever
        // PostMessageW's back to the UI thread (never SendMessageW's, and
        // never blocks waiting on the UI thread), so nothing here can be
        // waiting on this thread while this thread waits on it.
        //
        // Joining first also fixes the matching F-CRASH-03-class leaked-
        // ServerOperationResult defect: the window is no longer destroyed
        // while a worker's PostMessageW(WM_SERVER_OPERATION_DONE, ...) can
        // still be in flight against it, because that worker has already
        // finished (and already posted, if it was going to) by the time
        // this join returns.
        if (m_worker.joinable()) {
            m_worker.join();
        }
        if (DLNAServer.IsRunning()) {
            DLNAServer.Stop();
        }
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_COPYDATA: {
        const COPYDATASTRUCT* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (cds && cds->dwData == kCopyDataSourceReplace && cds->lpData &&
            IsPlausibleWideStringCopyDataSize(cds->cbData)) {
            // Construct with an EXPLICIT length derived from cbData -- the
            // one value Windows itself guarantees describes lpData's real
            // size (see COPYDATASTRUCT docs) -- instead of scanning lpData
            // for a null terminator with no upper bound. See F-CMR-01.
            const size_t charCount = cds->cbData / sizeof(wchar_t);
            std::wstring payload(reinterpret_cast<const wchar_t*>(cds->lpData), charCount);
            // This project's own sender (main.cpp) includes the trailing
            // L'\0' in its cbData computation; strip any trailing NULs so
            // downstream parsing sees exactly the intended string content
            // regardless of whether a given sender included one.
            while (!payload.empty() && payload.back() == L'\0') {
                payload.pop_back();
            }
            std::vector<std::wstring> paths = ParseQuotedCommaList(payload);
            std::vector<MediaSource> overrideSources;
            for (const auto& path : paths) {
                if (!path.empty()) overrideSources.push_back({path});
            }
            if (IsBusy()) {
                // A start/stop/restart is already in flight for this
                // instance; drop this request rather than racing it. The
                // second process that sent this message simply gets no
                // effect -- matches the existing guard pattern used by
                // BeginStartServer/BeginStopServer/BeginRestartServer.
                return TRUE;
            }
            if (DLNAServer.IsRunning()) {
                BeginSourceOverrideRestart(std::move(overrideSources));
            } else {
                // Nothing is being served yet; there is no session to
                // interrupt. Just install the override so the next manual
                // Start() picks it up (Phase 1's effectiveMediaSources).
                AppConfig.SetRuntimeSourceOverride(overrideSources);
                RefreshSourceList();
            }
        }
        return TRUE;
    }
    case WM_TRAYICON: {
        switch (DecodeTrayNotifyEvent(static_cast<unsigned long>(lParam), TRAY_ID)) {
        case TrayNotifyAction::Activate:
            RestoreAndFocusMainWindow();
            break;
        case TrayNotifyAction::ShowMenu:
            ShowTrayMenu();
            break;
        case TrayNotifyAction::None:
            break;
        }
        return 0;
    }
    case WM_CLOSE: {
        if (ShouldCloseNow(DLNAServer.IsRunning(), IsBusy())) {
            DestroyWindow(hwnd);
        } else {
            if (m_state == ServerUiState::Stopping) {
                m_closePending.RequestCloseOnceStopped();
            }
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    }
    case WM_TIMER: {
        if (wParam == kInitialScanPollTimerId) {
            const bool scanInProgress = DLNAServer.IsInitialScanInProgress();
            if (scanInProgress != m_lastPolledScanInProgress) {
                m_lastPolledScanInProgress = scanInProgress;
                SetControlsForState();
                InvalidateRect(m_hwnd, NULL, TRUE);
            }
        }
        return 0;
    }
    case WM_POWERBROADCAST: {
        if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) {
            if (DLNAServer.IsRunning()) {
                LogPrint(L"System resumed from suspend; re-registering SSDP");
                const ConfigSnapshot cfg = AppConfig.Snapshot();
                DLNAServer.RefreshEndpoints(cfg);
                SSDP::Get().Stop();
                SSDP::Get().Start(DLNAServer.GetEndpoints(), cfg.port, cfg.serverName, cfg.deviceUUID);
            }
            return TRUE;
        }
        if (wParam == PBT_APMSUSPEND) {
            return TRUE;
        }
        return FALSE;
    }
    case WM_DESTROY: {
        KillTimer(m_hwnd, kInitialScanPollTimerId);
        if (m_worker.joinable()) {
            m_worker.join();
        }
        if (m_hSuspendResumeNotify) {
            PowerUnregisterSuspendResumeNotification(m_hSuspendResumeNotify);
            m_hSuspendResumeNotify = NULL;
        }
        RemoveTrayIcon();
        DLNAServer.Stop();
        SetThreadExecutionState(ES_CONTINUOUS);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
