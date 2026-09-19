#ifndef MODAL_STACK_H
#define MODAL_STACK_H

#include <algorithm>
#include <cstddef>
#include <vector>

// Pure, platform-free ownership stack for "which subwindow currently owns the
// modal focus chain". No Win32 and no GTK types, so both front ends share one
// implementation and one CLI-testable behaviour. Mirrors the extraction pattern
// already used by source_list_focus.h, close_pending_state.h and
// server_close_policy.h.
//
// Win32 gets modality from EnableWindow(owner, FALSE) plus the
// ModalFocusSnapshot save/restore in modal_focus.h; GTK4 gets it from
// gtk_window_set_modal + gtk_window_set_transient_for. Neither platform can
// answer "which modal is topmost right now" on its own once dialogs nest
// (Settings opening Log opening a message box), which is the question this
// type answers.
//
// HandleT must be a pointer or an integer type. A default-constructed
// HandleT{} means "no window" and is never stored.
template <typename HandleT>
class ModalStack {
public:
    void Push(HandleT handle) {
        if (handle == HandleT{}) return;
        Remove(handle);
        m_stack.push_back(handle);
    }

    void Remove(HandleT handle) {
        m_stack.erase(std::remove(m_stack.begin(), m_stack.end(), handle), m_stack.end());
    }

    HandleT Top() const { return m_stack.empty() ? HandleT{} : m_stack.back(); }

    bool Contains(HandleT handle) const {
        return std::find(m_stack.begin(), m_stack.end(), handle) != m_stack.end();
    }

    bool Empty() const { return m_stack.empty(); }
    std::size_t Depth() const { return m_stack.size(); }
    void Clear() { m_stack.clear(); }

private:
    std::vector<HandleT> m_stack;
};

#endif // MODAL_STACK_H
