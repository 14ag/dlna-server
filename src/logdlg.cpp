#include "logdlg.h"
#include "log.h"
#include "modal_focus.h"
#include "../resources/resource.h"
#include <dwmapi.h>

namespace {

unsigned long long g_lastSeenSequence = 0;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#pragma comment(lib, "dwmapi.lib")

HFONT CreateLogFont(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) {
        ReleaseDC(hwnd, hdc);
    }
    return CreateFontW(-MulDiv(14, dpiY, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
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

void ApplyDarkFrame(HWND hwnd) {
    BOOL darkFrame = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
}

void LoadInitialLogText(HWND hwndDlg) {
    LogSnapshot initial = GetSystemLogSince(0);
    SetDlgItemTextW(hwndDlg, IDC_EDT_LOG_TEXT, initial.text.c_str());
    g_lastSeenSequence = initial.latestSequence;
    SendDlgItemMessage(hwndDlg, IDC_EDT_LOG_TEXT, EM_SETSEL, (WPARAM)-1, -1);
    SendDlgItemMessage(hwndDlg, IDC_EDT_LOG_TEXT, EM_SCROLLCARET, 0, 0);
}

void AppendNewLogText(HWND hwndDlg) {
    LogSnapshot delta = GetSystemLogSince(g_lastSeenSequence);
    if (delta.text.empty()) {
        g_lastSeenSequence = delta.latestSequence;
        return;
    }
    HWND edit = GetDlgItem(hwndDlg, IDC_EDT_LOG_TEXT);
    const int endPos = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, endPos, endPos);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(delta.text.c_str()));
    g_lastSeenSequence = delta.latestSequence;
}

}

INT_PTR LogDialog::Show(HWND hParent) {
    ModalFocusSnapshot focusSnapshot = CaptureModalFocus(hParent);
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_LOG), hParent, DialogProc, 0);
    RestoreModalFocus(focusSnapshot, hParent);
    return result;
}

INT_PTR CALLBACK LogDialog::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (uMsg) {
case WM_INITDIALOG: {
        ApplyDarkFrame(hwndDlg);
        ApplyDialogFont(hwndDlg);
        LoadInitialLogText(hwndDlg);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_REFRESH_LOG) {
            AppendNewLogText(hwndDlg);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hwndDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    case WM_DESTROY:
        break;
    }
    return (INT_PTR)FALSE;
}
