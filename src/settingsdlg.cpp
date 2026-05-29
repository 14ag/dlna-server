#include "settingsdlg.h"
#include "logdlg.h"
#include "dlna_utils.h"
#include "netutils.h"
#include "../resources/resource.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <cwctype>
#include <cstdio>
#include <string>

namespace {

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#pragma comment(lib, "dwmapi.lib")
const int IDC_PLAYLIST_MOVIE = 6101;
const int IDC_PLAYLIST_SUBTITLE = 6102;
const int IDC_PLAYLIST_ADD = 6103;
const int IDC_PLAYLIST_MOVIE_BROWSE = 6104;
const int IDC_PLAYLIST_SUBTITLE_BROWSE = 6105;
const int kDialogMargin = 16;
const int kLabelToControlGap = 12;
const int kRelatedControlGap = 8;
const int kLabelWidth = 84;
const int kControlHeight = 32;
const int kBrowseButtonWidth = 92;
const int kPlaylistDialogWidth = 552;
const int kPlaylistDialogHeight = 196;
const int kPlaylistEditLeft = kDialogMargin + kLabelWidth + kLabelToControlGap;
const int kPlaylistEditWidth = kPlaylistDialogWidth - (kDialogMargin * 2) - kLabelWidth - kLabelToControlGap - kRelatedControlGap - kBrowseButtonWidth;
const int kPlaylistBrowseLeft = kPlaylistEditLeft + kPlaylistEditWidth + kRelatedControlGap;
const int kPlaylistMovieTop = 16;
const int kPlaylistSubtitleTop = kPlaylistMovieTop + kControlHeight + 12;
const int kPlaylistAddTop = kPlaylistSubtitleTop + kControlHeight + 16;

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

HFONT DialogBodyFont(HWND hwnd) {
    static HFONT font = CreateScaledFont(hwnd, 14, FW_NORMAL, L"Segoe UI Variable Text");
    return font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void ApplyDarkFrame(HWND hwnd) {
    BOOL darkFrame = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
}

BOOL CALLBACK SetChildFontProc(HWND child, LPARAM fontParam) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(fontParam), TRUE);
    return TRUE;
}

void ApplyDialogFont(HWND hwnd) {
    HFONT font = DialogBodyFont(hwnd);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnumChildWindows(hwnd, SetChildFontProc, reinterpret_cast<LPARAM>(font));
}

std::wstring TrimWide(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && iswspace(value[start])) ++start;
    size_t end = value.size();
    while (end > start && iswspace(value[end - 1])) --end;
    return value.substr(start, end - start);
}

std::wstring GetDlgText(HWND hwnd, int id) {
    HWND item = GetDlgItem(hwnd, id);
    int len = GetWindowTextLengthW(item);
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(item, text.data(), len + 1);
    text.resize(len);
    return TrimWide(text);
}

std::wstring MovieTitleFromPath(const std::wstring& moviePath) {
    wchar_t fileName[MAX_PATH] = {};
    wcscpy_s(fileName, PathFindFileNameW(moviePath.c_str()));
    PathRemoveExtensionW(fileName);
    return fileName[0] ? fileName : L"Media item";
}

std::wstring BrowseFile(HWND owner, const wchar_t* title, const COMDLG_FILTERSPEC* filters, UINT filterCount) {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    std::wstring result;
    if (SUCCEEDED(hr)) {
        pFileOpen->SetTitle(title);
        if (filters && filterCount > 0) {
            pFileOpen->SetFileTypes(filterCount, filters);
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

void BrowsePlaylistEntryPath(HWND hwnd, int targetId, bool subtitle) {
    COMDLG_FILTERSPEC movieFilters[] = {
        { L"Movie files", L"*.mp4;*.m4v;*.mkv;*.webm;*.avi;*.mov;*.mpg;*.mpeg;*.ts;*.m2ts;*.wmv;*.flv;*.3gp;*.3g2" },
        { L"All files", L"*.*" },
    };
    COMDLG_FILTERSPEC subtitleFilters[] = {
        { L"Subtitle files", L"*.srt;*.vtt;*.sub;*.ass;*.ssa;*.smi;*.txt" },
        { L"All files", L"*.*" },
    };
    std::wstring selected = subtitle
        ? BrowseFile(hwnd, L"Choose subtitle file", subtitleFilters, 2)
        : BrowseFile(hwnd, L"Choose movie file", movieFilters, 2);
    if (!selected.empty()) {
        SetDlgItemTextW(hwnd, targetId, selected.c_str());
    }
}

bool FileHasBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
           ((static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow) > 0;
}

bool AppendUtf8Text(const std::wstring& path, const std::string& text) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"ab") != 0 || !fp) return false;
    fwrite(text.data(), 1, text.size(), fp);
    fclose(fp);
    return true;
}

bool AppendDefaultPlaylistEntry(const std::wstring& moviePath, const std::wstring& subtitlePath) {
    std::wstring playlistPath = AppConfig.defaultPlaylistPath.empty() ? AppConfig.GetDefaultPlaylistPath() : AppConfig.defaultPlaylistPath;
    bool hasContent = FileHasBytes(playlistPath);
    std::string text;
    if (!hasContent) {
        text += "#EXTM3U\n";
    }
    text += "#EXTINF:-1," + WideToUtf8(MovieTitleFromPath(moviePath)) + "\n";
    if (!subtitlePath.empty()) {
        text += "#DLNA-SUBTITLE:" + WideToUtf8(subtitlePath) + "\n";
        text += "#EXTVLCOPT:sub-file=" + WideToUtf8(subtitlePath) + "\n";
    }
    text += WideToUtf8(moviePath) + "\n";
    return AppendUtf8Text(playlistPath, text);
}

bool ReadPortFromDialog(HWND hwndDlg, int controlId, const wchar_t* label, int& port) {
    wchar_t text[32] = {};
    GetDlgItemTextW(hwndDlg, controlId, text, 32);
    int parsed = 0;
    if (!TryParsePortStrict(WideToUtf8(text), parsed)) {
        std::wstring message = std::wstring(label) + L" must be between 1 and 65535.";
        MessageBoxW(hwndDlg, message.c_str(), L"Invalid port", MB_ICONWARNING | MB_OK);
        SetFocus(GetDlgItem(hwndDlg, controlId));
        return false;
    }
    port = parsed;
    return true;
}

struct PlaylistEntryState {
    HWND owner = NULL;
    HWND movieEdit = NULL;
    HWND subtitleEdit = NULL;
    bool done = false;
};

void FinishPlaylistEntry(HWND hwnd, PlaylistEntryState* state, bool add) {
    if (add) {
        std::wstring movie = GetDlgText(hwnd, IDC_PLAYLIST_MOVIE);
        std::wstring subtitle = GetDlgText(hwnd, IDC_PLAYLIST_SUBTITLE);
        if (movie.empty()) {
            MessageBoxW(hwnd, L"Movie path is required.", L"Default playlist", MB_ICONWARNING | MB_OK);
            return;
        }
        if (!AppendDefaultPlaylistEntry(movie, subtitle)) {
            MessageBoxW(hwnd, L"Could not write default playlist.", L"Default playlist", MB_ICONWARNING | MB_OK);
            return;
        }
        AppConfig.defaultPlaylistEnabled = true;
        AppConfig.defaultPlaylistPath = AppConfig.defaultPlaylistPath.empty() ? AppConfig.GetDefaultPlaylistPath() : AppConfig.defaultPlaylistPath;
        AppConfig.Save();
    }
    state->done = true;
    DestroyWindow(hwnd);
}

LRESULT CALLBACK PlaylistEntryProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PlaylistEntryState* state = reinterpret_cast<PlaylistEntryState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<PlaylistEntryState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        ApplyDarkFrame(hwnd);
        HFONT font = DialogBodyFont(hwnd);
        HWND movieLabel = CreateWindowW(L"STATIC", L"Movie path:", WS_VISIBLE | WS_CHILD, kDialogMargin, kPlaylistMovieTop + 8, kLabelWidth, 18, hwnd, NULL, NULL, NULL);
        state->movieEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, kPlaylistEditLeft, kPlaylistMovieTop, kPlaylistEditWidth, kControlHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_MOVIE)), NULL, NULL);
        HWND movieBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_VISIBLE | WS_CHILD | WS_TABSTOP, kPlaylistBrowseLeft, kPlaylistMovieTop, kBrowseButtonWidth, kControlHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_MOVIE_BROWSE)), NULL, NULL);
        HWND subtitleLabel = CreateWindowW(L"STATIC", L"Subtitle path:", WS_VISIBLE | WS_CHILD, kDialogMargin, kPlaylistSubtitleTop + 8, kLabelWidth, 18, hwnd, NULL, NULL, NULL);
        state->subtitleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, kPlaylistEditLeft, kPlaylistSubtitleTop, kPlaylistEditWidth, kControlHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_SUBTITLE)), NULL, NULL);
        HWND subtitleBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_VISIBLE | WS_CHILD | WS_TABSTOP, kPlaylistBrowseLeft, kPlaylistSubtitleTop, kBrowseButtonWidth, kControlHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_SUBTITLE_BROWSE)), NULL, NULL);
        HWND add = CreateWindowW(L"BUTTON", L"Add", WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, kPlaylistBrowseLeft, kPlaylistAddTop, kBrowseButtonWidth, kControlHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_ADD)), NULL, NULL);
        HWND controls[] = { movieLabel, state->movieEdit, movieBrowse, subtitleLabel, state->subtitleEdit, subtitleBrowse, add };
        for (HWND control : controls) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(state->movieEdit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PLAYLIST_ADD) {
            FinishPlaylistEntry(hwnd, state, true);
            return 0;
        }
        if (LOWORD(wParam) == IDC_PLAYLIST_MOVIE_BROWSE) {
            BrowsePlaylistEntryPath(hwnd, IDC_PLAYLIST_MOVIE, false);
            return 0;
        }
        if (LOWORD(wParam) == IDC_PLAYLIST_SUBTITLE_BROWSE) {
            BrowsePlaylistEntryPath(hwnd, IDC_PLAYLIST_SUBTITLE, true);
            return 0;
        }
        break;
    case WM_CLOSE:
        FinishPlaylistEntry(hwnd, state, false);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}

INT_PTR SettingsDialog::Show(HWND hParent) {
    return DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hParent, DialogProc, 0);
}

void SettingsDialog::OnInitDialog(HWND hwndDlg) {
    ApplyDialogFont(hwndDlg);

    auto& cfg = AppConfig;
    SetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_NAME, cfg.serverName.c_str());
    SetDlgItemInt(hwndDlg, IDC_EDT_PORT, cfg.port, FALSE);
    SetDlgItemInt(hwndDlg, IDC_EDT_FILESERVER_PORT, cfg.fileServerPort, FALSE);
    SetDlgItemTextW(hwndDlg, IDC_EDT_IP_WHITELIST, cfg.ipWhiteList.c_str());

    CheckDlgButton(hwndDlg, IDC_CHK_RUN_ON_BOOT, cfg.runOnBoot ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_DEBUG_LOG, cfg.debugLog ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_DEFAULT_PLAYLIST, cfg.defaultPlaylistEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_ADD_ARTIST_ALBUM, cfg.addArtistAlbumFolders ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_HIDE_ALL_MEDIA, cfg.doNotShowAllMediaFolders ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_FLAT_FOLDERS, cfg.flatFolderStyle ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_SHOW_FILE_NAMES, cfg.showFileNamesInsteadOfTitles ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_SORT_BY_TITLE, cfg.sortByTitle ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_PROXY_STREAMS, cfg.proxyStreams ? BST_CHECKED : BST_UNCHECKED);
    UpdateDefaultPlaylistButton(hwndDlg);
}

bool SettingsDialog::OnOK(HWND hwndDlg) {
    auto& cfg = AppConfig;
    
    wchar_t buf[1024];
    GetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_NAME, buf, 1024);
    cfg.serverName = buf;
    
    int httpPort = 0;
    int fileServerPort = 0;
    if (!ReadPortFromDialog(hwndDlg, IDC_EDT_PORT, L"HTTP port", httpPort) ||
        !ReadPortFromDialog(hwndDlg, IDC_EDT_FILESERVER_PORT, L"File server port", fileServerPort)) {
        return false;
    }
    cfg.port = httpPort;
    cfg.fileServerPort = fileServerPort;
    
    GetDlgItemTextW(hwndDlg, IDC_EDT_IP_WHITELIST, buf, 1024);
    cfg.ipWhiteList = buf;

    cfg.runOnBoot = (IsDlgButtonChecked(hwndDlg, IDC_CHK_RUN_ON_BOOT) == BST_CHECKED);
    cfg.debugLog = (IsDlgButtonChecked(hwndDlg, IDC_CHK_DEBUG_LOG) == BST_CHECKED);
    cfg.defaultPlaylistEnabled = (IsDlgButtonChecked(hwndDlg, IDC_CHK_DEFAULT_PLAYLIST) == BST_CHECKED);
    if (cfg.defaultPlaylistPath.empty()) cfg.defaultPlaylistPath = cfg.GetDefaultPlaylistPath();
    cfg.addArtistAlbumFolders = (IsDlgButtonChecked(hwndDlg, IDC_CHK_ADD_ARTIST_ALBUM) == BST_CHECKED);
    cfg.doNotShowAllMediaFolders = (IsDlgButtonChecked(hwndDlg, IDC_CHK_HIDE_ALL_MEDIA) == BST_CHECKED);
    cfg.flatFolderStyle = (IsDlgButtonChecked(hwndDlg, IDC_CHK_FLAT_FOLDERS) == BST_CHECKED);
    cfg.showFileNamesInsteadOfTitles = (IsDlgButtonChecked(hwndDlg, IDC_CHK_SHOW_FILE_NAMES) == BST_CHECKED);
    cfg.sortByTitle = (IsDlgButtonChecked(hwndDlg, IDC_CHK_SORT_BY_TITLE) == BST_CHECKED);
    cfg.proxyStreams = (IsDlgButtonChecked(hwndDlg, IDC_CHK_PROXY_STREAMS) == BST_CHECKED);
    
    cfg.Save();
    return true;
}

void SettingsDialog::UpdateDefaultPlaylistButton(HWND hwndDlg) {
    BOOL enabled = IsDlgButtonChecked(hwndDlg, IDC_CHK_DEFAULT_PLAYLIST) == BST_CHECKED;
    EnableWindow(GetDlgItem(hwndDlg, IDC_BTN_DEFAULT_PLAYLIST_ADD), enabled);
}

void SettingsDialog::ShowPlaylistEntryForm(HWND hwndDlg) {
    const wchar_t* className = L"dlna-server_PlaylistEntry";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = PlaylistEntryProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    PlaylistEntryState state;
    state.owner = hwndDlg;
    RECT ownerRect = {};
    GetWindowRect(hwndDlg, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - kPlaylistDialogWidth) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - kPlaylistDialogHeight) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"Default playlist entry",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, kPlaylistDialogWidth, kPlaylistDialogHeight, hwndDlg, NULL, GetModuleHandleW(NULL), &state);
    if (!hwnd) return;
    EnableWindow(hwndDlg, FALSE);
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
    EnableWindow(hwndDlg, TRUE);
    SetForegroundWindow(hwndDlg);
    if (getResult == 0) {
        PostQuitMessage(static_cast<int>(msg.wParam));
    }
}

INT_PTR CALLBACK SettingsDialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        ApplyDarkFrame(hwndDlg);
        OnInitDialog(hwndDlg);
        return (INT_PTR)TRUE;
        
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (OnOK(hwndDlg)) EndDialog(hwndDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hwndDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_BTN_VIEW_LOG) {
            LogDialog::Show(hwndDlg);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_CHK_DEFAULT_PLAYLIST) {
            UpdateDefaultPlaylistButton(hwndDlg);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_BTN_DEFAULT_PLAYLIST_ADD) {
            ShowPlaylistEntryForm(hwndDlg);
            CheckDlgButton(hwndDlg, IDC_CHK_DEFAULT_PLAYLIST, BST_CHECKED);
            UpdateDefaultPlaylistButton(hwndDlg);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_BTN_RESTART) {
            if (OnOK(hwndDlg)) EndDialog(hwndDlg, IDC_BTN_RESTART);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
