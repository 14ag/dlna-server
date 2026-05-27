#include "logdlg.h"
#include "log.h"
#include "../resources/resource.h"

namespace {

HFONT CreateLogFont(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) {
        ReleaseDC(hwnd, hdc);
    }
    return CreateFontW(-MulDiv(14, dpiY, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

HFONT LogFont(HWND hwnd) {
    static HFONT font = CreateLogFont(hwnd);
    return font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

BOOL CALLBACK SetChildFontProc(HWND child, LPARAM fontParam) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(fontParam), TRUE);
    return TRUE;
}

void ApplyDialogFont(HWND hwnd) {
    HFONT font = LogFont(hwnd);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnumChildWindows(hwnd, SetChildFontProc, reinterpret_cast<LPARAM>(font));
}

}

INT_PTR LogDialog::Show(HWND hParent) {
    return DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_LOG), hParent, DialogProc, 0);
}

INT_PTR CALLBACK LogDialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        ApplyDialogFont(hwndDlg);
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
