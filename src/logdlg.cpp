#include "logdlg.h"
#include "log.h"
#include "modal_focus.h"
#include "ui_font.h"
#include "dark_frame.h"
#include "../resources/resource.h"
#include <dwmapi.h>

namespace {

unsigned long long g_lastSeenSequence = 0;

#pragma comment(lib, "dwmapi.lib")

HFONT LogFont(HWND hwnd) {
    static HFONT font = CreateScaledFont(hwnd, 14, FW_NORMAL, L"Segoe UI Variable Text");
    return font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void ApplyDialogFont(HWND hwnd) {
    ApplyFontToWindowAndChildren(hwnd, LogFont(hwnd));
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

static HHOOK g_logBackspaceHook = NULL;

static LRESULT CALLBACK LogBackspaceHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    if (nCode >= 0) {
        MSG* msg = reinterpret_cast<MSG*>(lParam);
        if (msg->message == WM_KEYDOWN && msg->wParam == VK_BACK) {
            wchar_t className[32] = {};
            GetClassNameW(msg->hwnd, className, 32);
            bool isLiveEdit = _wcsicmp(className, L"EDIT") == 0 &&
                               !(GetWindowLongW(msg->hwnd, GWL_STYLE) & ES_READONLY);
            if (!isLiveEdit) {
                PostMessageW(GetParent(msg->hwnd), WM_CLOSE, 0, 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

INT_PTR LogDialog::Show(HWND hParent) {
    ModalFocusSnapshot focusSnapshot = CaptureModalFocus(hParent);
    g_logBackspaceHook = SetWindowsHookExW(WH_MSGFILTER, LogBackspaceHookProc, NULL, GetCurrentThreadId());
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDD_LOG), hParent, DialogProc, 0);
    if (g_logBackspaceHook) UnhookWindowsHookEx(g_logBackspaceHook);
    g_logBackspaceHook = NULL;
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
