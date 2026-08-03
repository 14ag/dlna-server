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

} // namespace PosixTray

#endif // DLNA_POSIX_GUI

#endif // POSIX_TRAY_H
