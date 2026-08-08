#ifndef TRAY_NOTIFY_H
#define TRAY_NOTIFY_H

enum class TrayNotifyAction {
    None,
    Activate,
    ShowMenu
};

// decodes a notifyicon version four tray callback message see the
// workflow document task three for the exact microsoft documentation
// this follows
// rawLParam is the lparam value delivered to the window procedure for
// the tray icons ucallbackmessage
// expectedIconId is the uid the tray icon was created with
// returns none when the icon id encoded in rawLParam does not match
// expectedIconId or the event is not one this app reacts to
TrayNotifyAction DecodeTrayNotifyEvent(unsigned long rawLParam, unsigned short expectedIconId);

#endif // TRAY_NOTIFY_H
