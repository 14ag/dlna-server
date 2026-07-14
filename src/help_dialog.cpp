#include "help_dialog.h"
#include "modal_focus.h"
#include "cli_flags.h"
#include "settings_help.h"
#include <dwmapi.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

HFONT CreateHelpFont(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return CreateFontW(-MulDiv(14, dpiY, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
}

HFONT HelpBodyFont(HWND hwnd) {
    static HFONT font = CreateHelpFont(hwnd);
    return font ? font : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void ApplyDarkFrame(HWND hwnd) {
    BOOL darkFrame = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkFrame, sizeof(darkFrame));
}

struct HelpDialogState {
    bool done = false;
    ModalFocusSnapshot focusSnapshot;
    HWND owner = NULL;
};

LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<HelpDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        state = reinterpret_cast<HelpDialogState*>(cs->lpCreateParams);
        ApplyDarkFrame(hwnd);
        HFONT font = HelpBodyFont(hwnd);
        HWND edit = CreateWindowW(L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            10, 10, 380, 310, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        std::wstring text = L"Command-Line Flags\n\n";
        for (auto& f : GetCliFlagTable()) {
            text += f.flag + L" -- " + f.meaning + L"\n";
        }
        text += L"\n\nSettings\n\n";
        for (auto& s : GetSettingsHelpTable()) {
            text += s.label + L"\n  " + s.meaning + L"\n";
        }
        SetWindowTextW(edit, text.c_str());
        return 0;
    }
    case WM_CLOSE:
        if (state) {
            EnableOwnerAndRestoreModalFocus(state->focusSnapshot, state->owner);
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state && !state->done) state->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}

void HelpDialog::Show(HWND hParent) {
    const wchar_t* className = L"dlna-server_HelpDialog";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HelpWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    const int width = 400;
    const int height = 330;

    HelpDialogState state;
    state.owner = hParent;
    state.focusSnapshot = CaptureModalFocus(hParent);
    RECT ownerRect = {};
    GetWindowRect(hParent, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, className, L"DLNA Server Help",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, width, height, hParent, NULL, GetModuleHandleW(NULL), &state);
    if (!hwnd) return;
    EnableWindow(hParent, FALSE);
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
        EnableOwnerAndRestoreModalFocus(state.focusSnapshot, hParent);
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
    }
    if (getResult == 0) {
        PostQuitMessage(static_cast<int>(msg.wParam));
    }
}
