#include "help_dialog.h"
#include "modal_focus.h"
#include "cli_flags.h"
#include "settings_help.h"
#include <dwmapi.h>
#include <string>
#include <Richedit.h>

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

        // load the modern richedit control class once per process
        static bool g_richEditLoaded = []() {
            return LoadLibraryW(L"Msftedit.dll") != NULL;
        }();
        (void)g_richEditLoaded;

        HWND edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            10, 10, 380, 310, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        if (font) {
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        // set tab stop for two-column layout (flag column vs description column)
        // EM_SETTABSTOPS uses dialog units; at default 96dpi, 1 dialog unit = 1/4 character
        int tabStop = 160;
        SendMessageW(edit, EM_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&tabStop));

        // build text with tab between flag and description
        std::wstring text = L"Command-Line Flags\n\n";
        for (auto& f : GetCliFlagTable()) {
            text += f.flag + L"\t" + f.meaning + L"\n";
        }
        text += L"\nSettings\n\n";
        for (auto& s : GetSettingsHelpTable()) {
            text += s.label + L"\t" + s.meaning + L"\n";
        }

        SetWindowTextW(edit, text.c_str());

        // apply bold formatting to section headers
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_BOLD;
        cf.dwEffects = CFE_BOLD;

        // locate and bold each header using EM_FINDTEXTW
        struct { const wchar_t* text; int len; } headers[] = {
            { L"Command-Line Flags", 19 },
            { L"Settings", 8 }
        };
        for (auto& h : headers) {
            FINDTEXTEXW ft = {};
            ft.chrg.cpMin = 0;
            ft.chrg.cpMax = -1;
            ft.lpstrText = const_cast<wchar_t*>(h.text);
            LRESULT pos = SendMessageW(edit, EM_FINDTEXTW, 1, reinterpret_cast<LPARAM>(&ft));
            if (pos != -1) {
                CHARRANGE cr = {};
                cr.cpMin = static_cast<LONG>(pos);
                cr.cpMax = cr.cpMin + h.len;
                SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
                SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
            }
        }

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
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_MINIMIZEBOX, x, y, width, height, hParent, NULL, GetModuleHandleW(NULL), &state);
    if (!hwnd) return;
    EnableWindow(hParent, FALSE);
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
        DispatchMessage(&msg);
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
