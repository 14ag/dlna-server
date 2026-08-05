#include "posix_tray.h"

#ifdef DLNA_POSIX_GUI

#include <gio/gio.h>
#include <mutex>
#include <string>

#include "log.h"

namespace PosixTray {

namespace {

const char kTrayInterface[] = "org.kde.StatusNotifierItem";
const char kWatcherInterface[] = "org.kde.StatusNotifierWatcher";
const char kWatcherPath[] = "/StatusNotifierWatcher";
const char kTrayPath[] = "/StatusNotifierItem";
const char kMenuPath[] = "/MenuBar";

struct TrayState {
    std::string iconName;
    std::string title;
    std::function<void(TrayNotifyAction)> action;
    GDBusConnection* connection = nullptr;
    guint registeredObjectId = 0;
    guint exportedMenuId = 0;
    bool registrationConfirmed = false;
    bool trayAvailable = false;
};

std::mutex g_mutex;
TrayState g_state;

const char* StringOrEmpty(const char* value) {
    return value ? value : "";
}

GVariant* PropertyValue(const char* name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_strcmp0(name, "Category") == 0) {
        return g_variant_new_string("ApplicationStatus");
    }
    if (g_strcmp0(name, "Id") == 0) {
        return g_variant_new_string("dlna-server");
    }
    if (g_strcmp0(name, "Title") == 0) {
        return g_variant_new_string(g_state.title.c_str());
    }
    if (g_strcmp0(name, "Status") == 0) {
        return g_variant_new_string("Active");
    }
    if (g_strcmp0(name, "WindowId") == 0) {
        return g_variant_new_int32(0);
    }
    if (g_strcmp0(name, "IconName") == 0) {
        return g_variant_new_string(g_state.iconName.c_str());
    }
    if (g_strcmp0(name, "IconPixmap") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
        return g_variant_builder_end(&builder);
    }
    if (g_strcmp0(name, "OverlayIconName") == 0) {
        return g_variant_new_string("");
    }
    if (g_strcmp0(name, "AttentionIconName") == 0) {
        return g_variant_new_string("");
    }
    if (g_strcmp0(name, "AttentionMovieName") == 0) {
        return g_variant_new_string("");
    }
    if (g_strcmp0(name, "ToolTip") == 0) {
        GVariantBuilder pixmapBuilder;
        g_variant_builder_init(&pixmapBuilder, G_VARIANT_TYPE("a(iiay)"));
        GVariantBuilder tupleBuilder;
        g_variant_builder_init(&tupleBuilder, G_VARIANT_TYPE("(sa(iiay)ss)"));
        g_variant_builder_add(&tupleBuilder, "s", g_state.iconName.c_str());
        g_variant_builder_add_value(&tupleBuilder,
                                    g_variant_builder_end(&pixmapBuilder));
        g_variant_builder_add(&tupleBuilder, "s", g_state.title.c_str());
        g_variant_builder_add(&tupleBuilder, "s", g_state.title.c_str());
        return g_variant_builder_end(&tupleBuilder);
    }
    if (g_strcmp0(name, "Menu") == 0) {
        return g_variant_new_object_path(kMenuPath);
    }
    if (g_strcmp0(name, "ItemIsMenu") == 0) {
        return g_variant_new_boolean(true);
    }
    if (g_strcmp0(name, "IconThemePath") == 0) {
        return g_variant_new_string("");
    }
    return nullptr;
}

GVariant* HandleGetProperty(GDBusConnection*,
                            const gchar*,
                            const gchar*,
                            const gchar*,
                            const gchar* propertyName,
                            GError**,
                            gpointer) {
    GVariant* value = PropertyValue(propertyName);
    if (value == nullptr) {
        return nullptr;
    }
    return value;
}

void HandleActivate(GDBusConnection*,
                    const gchar*,
                    const gchar*,
                    const gchar*,
                    GVariant*,
                    GDBusMethodInvocation* invocation,
                    gpointer) {
    std::function<void(TrayNotifyAction)> action;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        action = g_state.action;
    }
    if (action) {
        action(TrayNotifyAction::Activate);
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

void HandleContextMenu(GDBusConnection*,
                       const gchar*,
                       const gchar*,
                       const gchar*,
                       GVariant*,
                       GDBusMethodInvocation* invocation,
                       gpointer) {
    std::function<void(TrayNotifyAction)> action;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        action = g_state.action;
    }
    if (action) {
        action(TrayNotifyAction::ShowMenu);
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

const GDBusInterfaceVTable kInterfaceVTable = {
    [](GDBusConnection* connection, const gchar* sender, const gchar* path,
       const gchar* interfaceName, const gchar* methodName, GVariant* parameters,
       GDBusMethodInvocation* invocation, gpointer userData) {
        if (g_strcmp0(methodName, "Activate") == 0) {
            HandleActivate(connection, sender, path, interfaceName,
                           parameters, invocation, userData);
            return;
        }
        if (g_strcmp0(methodName, "ContextMenu") == 0 ||
            g_strcmp0(methodName, "SecondaryActivate") == 0) {
            HandleContextMenu(connection, sender, path, interfaceName,
                              parameters, invocation, userData);
            return;
        }
        if (g_strcmp0(methodName, "Scroll") == 0) {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method %s", methodName);
    },
    HandleGetProperty,
    nullptr,
    {0},
};

const gchar kInterfaceXml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='i' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='OverlayIconName' type='s' access='read'/>"
    "    <property name='AttentionIconName' type='s' access='read'/>"
    "    <property name='AttentionMovieName' type='s' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='IconThemePath' type='s' access='read'/>"
    "    <method name='ContextMenu'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='Activate'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg type='i' name='delta' direction='in'/>"
    "      <arg type='s' name='orientation' direction='in'/>"
    "    </method>"
    "    <signal name='NewTitle'>"
    "      <arg type='s' name='title'/>"
    "    </signal>"
    "    <signal name='NewIcon'>"
    "      <arg type='s' name='iconName'/>"
    "    </signal>"
    "    <signal name='NewStatus'>"
    "      <arg type='s' name='status'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

void OnWatcherCallFinished(GObject* source, GAsyncResult* result, gpointer) {
    g_autoptr(GError) error = nullptr;
    g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.registrationConfirmed = true;
    g_state.trayAvailable = (error == nullptr);
    // A missing StatusNotifierWatcher is a normal, unremarkable
    // desktop/compositor configuration (WSLg does not provide one as
    // of this writing) -- do not surface this to the user as an
    // error. It is still logged so a maintainer reading debug.log can
    // tell tray-based window recovery is unavailable on this session
    // and confirm the fallback path (Task 14) is what is in effect.
    if (error != nullptr) {
        LogPrint(L"Tray icon unavailable: no StatusNotifierWatcher registered (%hs)", error->message);
    } else {
        LogPrint(L"Tray icon registered with StatusNotifierWatcher.");
    }
}

} // namespace

void Initialize(GDBusConnection* connection,
                const char* iconName,
                const char* title,
                GMenuModel* menu,
                std::function<void(TrayNotifyAction)> action) {
    if (connection == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.registeredObjectId != 0) {
        return;
    }
    g_state.connection = connection;
    g_state.iconName = StringOrEmpty(iconName);
    g_state.title = StringOrEmpty(title);
    g_state.action = std::move(action);

    g_autoptr(GDBusNodeInfo) nodeInfo =
        g_dbus_node_info_new_for_xml(kInterfaceXml, nullptr);
    GDBusInterfaceInfo* interfaceInfo =
        g_dbus_node_info_lookup_interface(nodeInfo, kTrayInterface);

    g_state.registeredObjectId = g_dbus_connection_register_object(
        connection, kTrayPath, interfaceInfo, &kInterfaceVTable, nullptr,
        nullptr, nullptr);

    g_autoptr(GMenuModel) exportedMenu = menu != nullptr
        ? G_MENU_MODEL(g_object_ref(menu))
        : G_MENU_MODEL(g_menu_new());
    g_state.exportedMenuId = g_dbus_connection_export_menu_model(
        connection, kMenuPath, exportedMenu, nullptr);

    const gchar* serviceName = g_dbus_connection_get_unique_name(connection);
    g_autofree gchar* senderName =
        g_strdup(serviceName != nullptr ? serviceName : "");
    g_dbus_connection_call(
        connection, "org.kde.StatusNotifierWatcher", kWatcherPath,
        kWatcherInterface, "RegisterStatusNotifierItem",
        g_variant_new("(s)", senderName), nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
        nullptr, OnWatcherCallFinished, nullptr);
}

void SetStatus(const char* iconName, const char* title) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.iconName = StringOrEmpty(iconName);
    g_state.title = StringOrEmpty(title);
    if (g_state.connection == nullptr || g_state.registeredObjectId == 0) {
        return;
    }
    g_dbus_connection_emit_signal(
        g_state.connection, nullptr, kTrayPath, kTrayInterface, "NewIcon",
        g_variant_new("(s)", g_state.iconName.c_str()), nullptr);
    g_dbus_connection_emit_signal(
        g_state.connection, nullptr, kTrayPath, kTrayInterface, "NewTitle",
        g_variant_new("(s)", g_state.title.c_str()), nullptr);
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.connection == nullptr) {
        return;
    }
    if (g_state.exportedMenuId != 0) {
        g_dbus_connection_unexport_menu_model(g_state.connection,
                                              g_state.exportedMenuId);
    }
    if (g_state.registeredObjectId != 0) {
        g_dbus_connection_unregister_object(g_state.connection,
                                            g_state.registeredObjectId);
    }
    g_state.connection = nullptr;
    g_state.registeredObjectId = 0;
    g_state.exportedMenuId = 0;
}

bool IsRegistrationConfirmed() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state.registrationConfirmed;
}

bool IsTrayAvailable() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state.trayAvailable;
}

} // namespace PosixTray

#endif // DLNA_POSIX_GUI
