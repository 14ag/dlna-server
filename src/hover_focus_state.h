#ifndef HOVER_FOCUS_STATE_H
#define HOVER_FOCUS_STATE_H

class HoverFocusState {
public:
    static constexpr int kNoControl = -1;

    void OnMouseEnter(int controlId) { m_hoveredId = controlId; }

    void OnMouseLeave(int controlId) {
        if (m_hoveredId == controlId) m_hoveredId = kNoControl;
    }

    void OnFocusGained(int controlId) { m_focusedId = controlId; }

    void OnFocusLost(int controlId) {
        if (m_focusedId == controlId) m_focusedId = kNoControl;
    }

    int HighlightedControlId() const {
        return m_hoveredId != kNoControl ? m_hoveredId : m_focusedId;
    }

    int HoveredControlId() const { return m_hoveredId; }
    int FocusedControlId() const { return m_focusedId; }

private:
    int m_hoveredId = kNoControl;
    int m_focusedId = kNoControl;
};

#endif // HOVER_FOCUS_STATE_H
