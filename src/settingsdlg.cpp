#include "settingsdlg.h"
#include "logdlg.h"
#include "../resources/resource.h"
#include <commctrl.h>
#include <string>

INT_PTR SettingsDialog::Show(HWND hParent) {
    return DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hParent, DialogProc, 0);
}

void SettingsDialog::OnInitDialog(HWND hwndDlg) {
    auto& cfg = AppConfig;
    
    SetDlgItemTextW(hwndDlg, IDC_EDT_SERVER_NAME, cfg.serverName.c_str());
    SetDlgItemInt(hwndDlg, IDC_EDT_PORT, cfg.port, FALSE);
    SetDlgItemInt(hwndDlg, IDC_EDT_FILESERVER_PORT, cfg.fileServerPort, FALSE);
    SetDlgItemTextW(hwndDlg, IDC_EDT_IP_WHITELIST, cfg.ipWhiteList.c_str());
    
    CheckDlgButton(hwndDlg, IDC_CHK_RUN_ON_BOOT, cfg.runOnBoot ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_DEBUG_LOG, cfg.debugLog ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_ADD_ARTIST_ALBUM, cfg.addArtistAlbumFolders ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_HIDE_ALL_MEDIA, cfg.doNotShowAllMediaFolders ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_FLAT_FOLDERS, cfg.flatFolderStyle ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_SHOW_FILE_NAMES, cfg.showFileNamesInsteadOfTitles ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_SORT_BY_TITLE, cfg.sortByTitle ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwndDlg, IDC_CHK_PROXY_STREAMS, cfg.proxyStreams ? BST_CHECKED : BST_UNCHECKED);
    
    // Disable deferred feature controls
    EnableWindow(GetDlgItem(hwndDlg, IDC_CHK_THUMB_VIDEO), FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CHK_THUMB_AUDIO), FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CHK_THUMB_IMAGE), FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_CMB_THUMB_QUALITY), FALSE);
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
    
    cfg.runOnBoot = (IsDlgButtonChecked(hwndDlg, IDC_CHK_RUN_ON_BOOT) == BST_CHECKED);
    cfg.debugLog = (IsDlgButtonChecked(hwndDlg, IDC_CHK_DEBUG_LOG) == BST_CHECKED);
    cfg.addArtistAlbumFolders = (IsDlgButtonChecked(hwndDlg, IDC_CHK_ADD_ARTIST_ALBUM) == BST_CHECKED);
    cfg.doNotShowAllMediaFolders = (IsDlgButtonChecked(hwndDlg, IDC_CHK_HIDE_ALL_MEDIA) == BST_CHECKED);
    cfg.flatFolderStyle = (IsDlgButtonChecked(hwndDlg, IDC_CHK_FLAT_FOLDERS) == BST_CHECKED);
    cfg.showFileNamesInsteadOfTitles = (IsDlgButtonChecked(hwndDlg, IDC_CHK_SHOW_FILE_NAMES) == BST_CHECKED);
    cfg.sortByTitle = (IsDlgButtonChecked(hwndDlg, IDC_CHK_SORT_BY_TITLE) == BST_CHECKED);
    cfg.proxyStreams = (IsDlgButtonChecked(hwndDlg, IDC_CHK_PROXY_STREAMS) == BST_CHECKED);
    
    cfg.Save();
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
        else if (LOWORD(wParam) == IDC_BTN_RESTART) {
            // TODO: Restart server
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
