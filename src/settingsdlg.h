#ifndef SETTINGSDLG_H
#define SETTINGSDLG_H

#include <windows.h>
#include "config.h"

class SettingsDialog {
public:
    static INT_PTR Show(HWND hParent);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void OnInitDialog(HWND hwndDlg);
    static void OnOK(HWND hwndDlg);
    static void UpdateDefaultPlaylistButton(HWND hwndDlg);
    static void BrowseServerIcon(HWND hwndDlg);
    static void ShowPlaylistEntryForm(HWND hwndDlg);
};

#endif // SETTINGSDLG_H
