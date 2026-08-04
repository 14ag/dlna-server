#ifndef ACCESS_KEYS_GTK_ADAPTER_H
#define ACCESS_KEYS_GTK_ADAPTER_H

#include "access_keys.h"

// converts an already win32 style ampersand marked label from
// InsertMnemonicMarker into the single leading underscore gtk4 expects
// gtk_button_set_use_underline and gtk_label_set_use_underline both read
// this exact underscore convention
inline std::wstring ConvertAmpersandMnemonicToUnderscore(const std::wstring& label) {
    std::wstring result;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == L'&') {
            if (i + 1 < label.size() && label[i + 1] == L'&') {
                result += L'&';
                ++i;
            } else {
                result += L'_';
            }
        } else {
            result += label[i];
        }
    }
    return result;
}

#endif // ACCESS_KEYS_GTK_ADAPTER_H
