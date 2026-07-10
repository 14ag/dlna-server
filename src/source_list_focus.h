#ifndef SOURCE_LIST_FOCUS_H
#define SOURCE_LIST_FOCUS_H

// Pure state machine for the media-source listbox's Delete-button
// enablement rule. No Win32 types -- takes plain bools decided by the
// caller from WM_KILLFOCUS/LBN_SELCHANGE/deletion events. See the
// implementation guide, Task 14, for the exact rule this encodes:
//
//   NO-FOCUS is true by default.
//   NO-FOCUS is false whenever a source list item is selected.
//   NO-FOCUS becomes true when the user selects item(s) then clicks
//     anywhere other than the Delete button.
//   NO-FOCUS becomes true immediately after a source has been deleted.
//   The Delete button is enabled iff NO-FOCUS is false.
class SourceListFocusState {
public:
    SourceListFocusState() : m_noFocus(true) {}

    void OnSelectionChanged(bool hasSelection) {
        if (hasSelection) m_noFocus = false;
    }

    // gainedFocusIsDeleteButton: true if the window about to receive focus
    // (WM_KILLFOCUS's wParam) is the Delete button's HWND.
    void OnListBoxLostFocus(bool gainedFocusIsDeleteButton) {
        if (!gainedFocusIsDeleteButton) m_noFocus = true;
    }

    void OnSourceDeleted() { m_noFocus = true; }

    bool IsNoFocus() const { return m_noFocus; }

private:
    bool m_noFocus;
};

#endif // SOURCE_LIST_FOCUS_H