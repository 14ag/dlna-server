#ifndef MODAL_FOCUS_H
#define MODAL_FOCUS_H

#include <windows.h>

struct ModalFocusSnapshot {
    HWND owner = NULL;
    HWND foreground = NULL;
    bool foregroundWasOwned = false;
};

bool IsWindowInModalChain(HWND owner, HWND hwnd);
ModalFocusSnapshot CaptureModalFocus(HWND owner);
void RestoreModalFocus(const ModalFocusSnapshot& snapshot, HWND focusTarget);
void EnableOwnerAndRestoreModalFocus(const ModalFocusSnapshot& snapshot, HWND focusTarget);

#endif // MODAL_FOCUS_H
