#include "access_key_hook.h"

static HHOOK g_hook = NULL;

static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == PM_REMOVE) {
        MSG* msg = reinterpret_cast<MSG*>(lParam);
        HWND root = GetAncestor(msg->hwnd, GA_ROOT);
        switch (msg->message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CHAR:
            PostMessageW(root, WM_UPDATEUISTATE,
                MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL | UISF_HIDEFOCUS), 0);
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
            PostMessageW(root, WM_UPDATEUISTATE,
                MAKEWPARAM(UIS_SET, UISF_HIDEACCEL | UISF_HIDEFOCUS), 0);
            break;
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

bool InstallAccessKeyHook() {
    g_hook = SetWindowsHookExW(WH_GETMESSAGE, HookProc, NULL, GetCurrentThreadId());
    return g_hook != NULL;
}

void RemoveAccessKeyHook() {
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = NULL;
    }
}
