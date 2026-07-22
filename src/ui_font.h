#ifndef UI_FONT_H
#define UI_FONT_H

#include <windows.h>

// dpi aware font creation shared by every dialog in the app
// pixelSize is the desired font size measured at 96 dpi
// weight is a windows font weight constant such as FW_NORMAL or FW_SEMIBOLD
// faceName must stay valid only for the duration of this call
HFONT CreateScaledFont(HWND hwnd, int pixelSize, int weight, const wchar_t* faceName);

// sends WM_SETFONT to hwnd and to every direct and indirect child control
void ApplyFontToWindowAndChildren(HWND hwnd, HFONT font);

#endif
