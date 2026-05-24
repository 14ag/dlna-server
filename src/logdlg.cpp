#include "logdlg.h"
#include "log.h"
#include "../resources/resource.h"

INT_PTR LogDialog::Show(HWND hParent) {
    return DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_LOG), hParent, DialogProc, 0);
}

INT_PTR CALLBACK LogDialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        std::wstring logText = GetSystemLog();
        SetDlgItemTextW(hwndDlg, IDC_EDT_LOG_TEXT, logText.c_str());
        // Scroll to bottom
        SendDlgItemMessage(hwndDlg, IDC_EDT_LOG_TEXT, EM_SETSEL, 0, -1);
        SendDlgItemMessage(hwndDlg, IDC_EDT_LOG_TEXT, EM_SETSEL, (WPARAM)-1, -1);
        SendDlgItemMessage(hwndDlg, IDC_EDT_LOG_TEXT, EM_SCROLLCARET, 0, 0);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hwndDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
