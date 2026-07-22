#ifndef DARK_FRAME_H
#define DARK_FRAME_H

#include <windows.h>

// applies the windows immersive dark mode title bar to hwnd
// safe to call even on a windows version that predates this attribute
// the call is simply ignored by dwm in that case
void ApplyDarkFrame(HWND hwnd);

#endif
