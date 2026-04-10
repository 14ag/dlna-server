#ifndef LOGDLG_H
#define LOGDLG_H

#include <windows.h>

class LogDialog {
public:
    static INT_PTR Show(HWND hParent);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

#endif // LOGDLG_H
