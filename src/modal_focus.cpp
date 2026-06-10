#include "modal_focus.h"

bool IsWindowInModalChain(HWND owner, HWND hwnd) {
    if (!owner || !hwnd || !IsWindow(owner) || !IsWindow(hwnd)) return false;
    if (owner == hwnd || IsChild(owner, hwnd)) return true;

    if (GetWindow(hwnd, GW_OWNER) == owner) return true;
    HWND current = GetWindow(hwnd, GW_OWNER);
    while (current) {
        if (current == owner) return true;
        current = GetWindow(current, GW_OWNER);
    }

    HWND ownerRoot = GetAncestor(owner, GA_ROOTOWNER);
    HWND hwndRoot = GetAncestor(hwnd, GA_ROOTOWNER);
    return ownerRoot && hwndRoot && ownerRoot == hwndRoot;
}

ModalFocusSnapshot CaptureModalFocus(HWND owner) {
    ModalFocusSnapshot snapshot;
    snapshot.owner = owner;
    snapshot.foreground = GetForegroundWindow();
    snapshot.foregroundWasOwned = IsWindowInModalChain(owner, snapshot.foreground);
    return snapshot;
}

void RestoreModalFocus(const ModalFocusSnapshot& snapshot, HWND focusTarget) {
    if (!snapshot.owner || !IsWindow(snapshot.owner) || !IsWindowEnabled(snapshot.owner)) return;
    if (!snapshot.foregroundWasOwned) return;

    HWND currentForeground = GetForegroundWindow();
    if (currentForeground && !IsWindowInModalChain(snapshot.owner, currentForeground)) return;

    if (!focusTarget || !IsWindow(focusTarget) || !IsWindowEnabled(focusTarget)) {
        focusTarget = snapshot.owner;
    }

    HWND targetRoot = GetAncestor(focusTarget, GA_ROOT);
    if (targetRoot && IsWindowEnabled(targetRoot)) {
        SetActiveWindow(targetRoot);
    }
    SetFocus(focusTarget);
}

void EnableOwnerAndRestoreModalFocus(const ModalFocusSnapshot& snapshot, HWND focusTarget) {
    if (snapshot.owner && IsWindow(snapshot.owner) && !IsWindowEnabled(snapshot.owner)) {
        EnableWindow(snapshot.owner, TRUE);
    }
    RestoreModalFocus(snapshot, focusTarget);
}
