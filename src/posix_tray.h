#ifndef POSIX_TRAY_H
#define POSIX_TRAY_H

#ifdef DLNA_POSIX_GUI

#include "tray_notify.h"
#include <functional>
#include <gio/gio.h>

// org.kde.StatusNotifierItem tray icon for GTK4 builds
// GTK4 removed GtkStatusIcon and libayatana has no GTK4
// support so the tray talks D-Bus to the desktop watcher
namespace PosixTray {

void Initialize(GDBusConnection* connection,
                const char* iconName,
                const char* title,
                GMenuModel* menu,
                std::function<void(TrayNotifyAction)> action);

void SetStatus(const char* iconName, const char* title);

void Shutdown();

// True once RegisterStatusNotifierItem has completed, successfully or
// not. False before Initialize() is called or while the async D-Bus
// call is still in flight.
bool IsRegistrationConfirmed();
// True only if the async RegisterStatusNotifierItem call completed
// successfully. Always false on a desktop/compositor with no
// StatusNotifierWatcher, which includes WSLg as of this writing --
// see Task 14 of
// dlna-server-qa-audit-and-posix-gui-lifecycle-workflow-05-08-26.md.
bool IsTrayAvailable();

} // namespace PosixTray

#endif // DLNA_POSIX_GUI

#endif // POSIX_TRAY_H
