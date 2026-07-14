#include "access_keys.h"
#include <algorithm>
#include <cwctype>

static bool IsSkippable(wchar_t ch) {
    return iswspace(ch) || iswpunct(ch);
}

wchar_t AssignOneMnemonic(const std::wstring& label, const std::unordered_set<wchar_t>& used) {
    std::unordered_set<wchar_t> usedUpper;
    for (wchar_t c : used) {
        usedUpper.insert(static_cast<wchar_t>(towupper(c)));
    }
    for (wchar_t ch : label) {
        wchar_t upper = static_cast<wchar_t>(towupper(ch));
        if (!IsSkippable(ch) && usedUpper.find(upper) == usedUpper.end()) {
            return ch;
        }
    }
    return L'\0';
}

std::vector<wchar_t> AssignMnemonics(const std::vector<std::wstring>& labels) {
    std::vector<wchar_t> result;
    std::unordered_set<wchar_t> used;
    for (const auto& label : labels) {
        wchar_t m = AssignOneMnemonic(label, used);
        result.push_back(m);
        if (m != L'\0') {
            used.insert(static_cast<wchar_t>(towupper(m)));
        }
    }
    return result;
}

std::wstring InsertMnemonicMarker(const std::wstring& label, wchar_t assigned) {
    if (assigned == L'\0') return label;
    std::wstring escaped;
    for (wchar_t ch : label) {
        if (ch == L'&') escaped += L"&&";
        else escaped += ch;
    }
    wchar_t assignedLower = static_cast<wchar_t>(towlower(assigned));
    wchar_t assignedUpper = static_cast<wchar_t>(towupper(assigned));
    size_t pos = std::wstring::npos;
    for (size_t i = 0; i < escaped.size(); ++i) {
        if (escaped[i] == assignedLower || escaped[i] == assignedUpper) {
            pos = i;
            break;
        }
    }
    if (pos == std::wstring::npos) return escaped;
    escaped.insert(pos, 1, L'&');
    return escaped;
}

std::wstring StripMnemonicMarker(const std::wstring& label) {
    std::wstring result;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == L'&') {
            if (i + 1 < label.size() && label[i + 1] == L'&') {
                result += L'&';
                ++i;
            }
        } else {
            result += label[i];
        }
    }
    return result;
}
