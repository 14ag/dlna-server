#ifndef WIN_GEOMETRY_DUMP_H
#define WIN_GEOMETRY_DUMP_H

#include <windows.h>
#include "config.h"
#include "log.h"

// single shared geometry dump used by every win32 dialog so the
// gtk4 port has ground truth pixel values to transcribe
// modeled on the original LogSettingsControlGeometryProc walk in
// settingsdlg cpp but parameterized on the log tag so one walk
// serves every dialog

namespace {

struct GeometryDumpContext {
    HWND dlg;
    const wchar_t* tag;
};

BOOL CALLBACK DumpGeometryChildProc(HWND hwndChild, LPARAM lParam) {
    GeometryDumpContext* ctx = reinterpret_cast<GeometryDumpContext*>(lParam);
    RECT rc = {};
    GetWindowRect(hwndChild, &rc);
    POINT points[2] = { { rc.left, rc.top }, { rc.right, rc.bottom } };
    MapWindowPoints(NULL, ctx->dlg, points, 2);
    wchar_t className[64] = {};
    GetClassNameW(hwndChild, className, 64);
    wchar_t text[64] = {};
    GetWindowTextW(hwndChild, text, 64);
    // long multiline control text would embed newlines and truncate the
    // log line so cap the capture at a short prefix and blank out
    // newline characters to keep every geometry dump on one line
    for (wchar_t* p = text; *p; ++p) {
        if (*p == L'\r' || *p == L'\n') *p = L' ';
    }
    LogPrint(L"[%ls] class=%ls id=%d text=\"%ls\" x=%ld y=%ld w=%ld h=%ld",
             ctx->tag, className, GetDlgCtrlID(hwndChild), text,
             points[0].x, points[0].y,
             points[1].x - points[0].x, points[1].y - points[0].y);
    return TRUE;
}

void DumpDialogGeometry(HWND hwndDlg, const wchar_t* tag) {
    if (!AppConfig.IsDebugLogEnabled()) return;
    RECT client = {};
    GetClientRect(hwndDlg, &client);
    LogPrint(L"[%ls] class=dialog-client id=0 text=\"\" x=0 y=0 w=%ld h=%ld",
             tag, client.right, client.bottom);
    GeometryDumpContext ctx = { hwndDlg, tag };
    EnumChildWindows(hwndDlg, DumpGeometryChildProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace

#endif  // WIN_GEOMETRY_DUMP_H
