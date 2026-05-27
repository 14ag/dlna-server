#include "settingsdlg.h"
#include "logdlg.h"
#include "netutils.h"
#include "../resources/resource.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <cwctype>
#include <cstdio>
#include <string>

namespace {
const int IDC_PLAYLIST_MOVIE = 6101;
const int IDC_PLAYLIST_SUBTITLE = 6102;
const int IDC_PLAYLIST_ADD = 6103;

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
        HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HWND movieLabel = CreateWindowW(L"STATIC", L"Movie path:", WS_VISIBLE | WS_CHILD, 16, 18, 90, 18, hwnd, NULL, NULL, NULL);
        state->movieEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 112, 16, 300, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_MOVIE)), NULL, NULL);
        HWND subtitleLabel = CreateWindowW(L"STATIC", L"Subtitle path:", WS_VISIBLE | WS_CHILD, 16, 54, 90, 18, hwnd, NULL, NULL, NULL);
        state->subtitleEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 112, 52, 300, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_SUBTITLE)), NULL, NULL);
        HWND add = CreateWindowW(L"BUTTON", L"Add", WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 336, 92, 76, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PLAYLIST_ADD)), NULL, NULL);
        HWND controls[] = { movieLabel, state->movieEdit, subtitleLabel, state->subtitleEdit, add };
        for (HWND control : controls) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(state->movieEdit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PLAYLIST_ADD) {
            FinishPlaylistEntry(hwnd, state, true);
            return 0;
        }
        break;
    case WM_CLOSE:
        FinishPlaylistEntry(hwnd, state, false);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::wstring BrowseIconFile(HWND owner) {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    std::wstring result;
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC filters[] = {
            { L"Icon and image files", L"*.ico;*.png;*.jpg;*.jpeg" },
            { L"All files", L"*.*" },
        };
        pFileOpen->SetFileTypes(2, filters);
        pFileOpen->SetTitle(L"Choose server icon");
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
}

INT_PTR SettingsDialog::Show(HWND hParent) {
    return DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hParent, DialogProc, 0);
}

void SettingsDialog::OnInitDialog(HWND hwndDlg) {
    auto& cfg = AppConfig;
    
    SetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_NAME, cfg.serverName.c_str());
    SetDlgItemInt(hwndDlg, IDC_EDT_PORT, cfg.port, FALSE);
    SetDlgItemInt(hwndDlg, IDC_EDT_FILESERVER_PORT, cfg.fileServerPort, FALSE);
    SetDlgItemTextW(hwndDlg, IDC_EDT_IP_WHITELIST, cfg.ipWhiteList.c_str());
    SetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_ICON, cfg.serverIconPath.c_str());
    
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

void SettingsDialog::OnOK(HWND hwndDlg) {
    auto& cfg = AppConfig;
    
    wchar_t buf[1024];
    GetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_NAME, buf, 1024);
    cfg.serverName = buf;
    
    cfg.port = GetDlgItemInt(hwndDlg, IDC_EDT_PORT, NULL, FALSE);
    cfg.fileServerPort = GetDlgItemInt(hwndDlg, IDC_EDT_FILESERVER_PORT, NULL, FALSE);
    
    GetDlgItemTextW(hwndDlg, IDC_EDT_IP_WHITELIST, buf, 1024);
    cfg.ipWhiteList = buf;

    GetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_ICON, buf, 1024);
    cfg.serverIconPath = TrimWide(buf);
    
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
}

void SettingsDialog::UpdateDefaultPlaylistButton(HWND hwndDlg) {
    BOOL enabled = IsDlgButtonChecked(hwndDlg, IDC_CHK_DEFAULT_PLAYLIST) == BST_CHECKED;
    EnableWindow(GetDlgItem(hwndDlg, IDC_BTN_DEFAULT_PLAYLIST_ADD), enabled);
}

void SettingsDialog::BrowseServerIcon(HWND hwndDlg) {
    std::wstring selected = BrowseIconFile(hwndDlg);
    if (!selected.empty()) {
        SetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_ICON, selected.c_str());
    }
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
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - 450) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - 160) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"Default playlist entry",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, 450, 160, hwndDlg, NULL, GetModuleHandleW(NULL), &state);
    if (!hwnd) return;
    EnableWindow(hwndDlg, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG msg;
    while (!state.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hwndDlg, TRUE);
    SetForegroundWindow(hwndDlg);
}

INT_PTR CALLBACK SettingsDialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG:
        OnInitDialog(hwndDlg);
        return (INT_PTR)TRUE;
        
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            OnOK(hwndDlg);
            EndDialog(hwndDlg, LOWORD(wParam));
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
        else if (LOWORD(wParam) == IDC_BTN_SERVER_ICON_BROWSE) {
            BrowseServerIcon(hwndDlg);
            return (INT_PTR)TRUE;
        }
        else if (LOWORD(wParam) == IDC_BTN_RESTART) {
            OnOK(hwndDlg);
            EndDialog(hwndDlg, IDC_BTN_RESTART);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
