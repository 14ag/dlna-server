#ifndef ACCESS_KEYS_H
#define ACCESS_KEYS_H

#include <string>
#include <unordered_set>
#include <vector>

wchar_t AssignOneMnemonic(const std::wstring& label, const std::unordered_set<wchar_t>& used);
std::vector<wchar_t> AssignMnemonics(const std::vector<std::wstring>& labels);
std::wstring InsertMnemonicMarker(const std::wstring& label, wchar_t assigned);
std::wstring StripMnemonicMarker(const std::wstring& label);

class KeyboardCueState {
public:
    KeyboardCueState() : m_hideAccel(true), m_hideFocus(true) {}
    void OnKeyboardInput() { m_hideAccel = false; m_hideFocus = false; }
    void OnMouseButtonInput() { m_hideAccel = true; m_hideFocus = true; }
    bool HideAccel() const { return m_hideAccel; }
    bool HideFocus() const { return m_hideFocus; }
private:
    bool m_hideAccel;
    bool m_hideFocus;
};

#endif // ACCESS_KEYS_H
