#include "tray_notify.h"

namespace {
// raw win32 message and notification values are duplicated here as
// plain integer literals rather than including windows headers so
// this file and its logic can be exercised by a hidden cli print flag
// on both platforms see winuser h for the canonical definitions
constexpr unsigned long kWmLButtonUp = 0x0202UL;
constexpr unsigned long kWmLButtonDblClk = 0x0203UL;
constexpr unsigned long kWmRButtonUp = 0x0205UL;
constexpr unsigned long kWmContextMenu = 0x007BUL;
constexpr unsigned long kNinSelect = 0x0400UL;
constexpr unsigned long kNinKeySelect = 0x0401UL;
}

TrayNotifyAction DecodeTrayNotifyEvent(unsigned long rawLParam, unsigned short expectedIconId) {
    const unsigned long notifyEvent = rawLParam & 0xffffUL;
    const unsigned short iconId = static_cast<unsigned short>((rawLParam >> 16) & 0xffffUL);
    if (iconId != expectedIconId) {
        return TrayNotifyAction::None;
    }

    if (notifyEvent == kWmLButtonUp || notifyEvent == kWmLButtonDblClk ||
        notifyEvent == kNinSelect || notifyEvent == kNinKeySelect) {
        return TrayNotifyAction::Activate;
    }
    if (notifyEvent == kWmContextMenu || notifyEvent == kWmRButtonUp) {
        return TrayNotifyAction::ShowMenu;
    }
    return TrayNotifyAction::None;
}
