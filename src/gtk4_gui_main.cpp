// GTK4 front end for the DLNA server POSIX build
// pixel and color parity target is the Win32 GUI
// see src/mainwindow.cpp src/settingsdlg.cpp src/logdlg.cpp
// src/help_dialog.cpp and resources/app.rc
// plain GTK4 C API from C++ with no gtkmm layer
// NOTE the tray icon and dialog chrome replicate the Win32
// windows as closely as GTK4 allows
// NOTE message boxes use GtkMessageDialog because this build
// targets GTK 4.6 which predates GtkAlertDialog added in 4.10

#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_source_file_types.h"
#include "thread_guard.h"
#include "server.h"
#include "settings_restart.h"
#include "close_pending_state.h"
#include "server_close_policy.h"
#include "input_gate.h"
#include "settings_help.h"
#include "cli_flags.h"
#include "posix_single_instance.h"
#include "source_drop_policy.h"
#include "ui_tokens.h"
#include "server_ui_state.h"
#include "posix_tray.h"

#include <gtk/gtk.h>
#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_signalStop(false);

void HandleTerminationSignal(int) {
    g_signalStop.store(true, std::memory_order_relaxed);
}

std::string ToUtf8(const std::wstring& value) {
    return WideToUtf8(value);
}

std::wstring ToWide(const char* value) {
    return Utf8ToWide(value ? value : "");
}

std::wstring ToWideString(const gchar* value) {
    return Utf8ToWide(value ? value : "");
}

std::string TitleFromPath(const std::string& moviePath) {
    size_t slash = moviePath.find_last_of("/\\");
    std::string name = slash == std::string::npos ? moviePath : moviePath.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name.empty() ? "Media item" : name;
}

bool AppendDefaultPlaylistEntry(const std::string& moviePath, const std::string& subtitlePath) {
    if (moviePath.empty()) return false;
    if (AppConfig.defaultPlaylistPath.empty()) AppConfig.defaultPlaylistPath = AppConfig.GetDefaultPlaylistPath();
    std::ifstream existing(WideToUtf8(AppConfig.defaultPlaylistPath), std::ios::binary);
    bool hasContent = existing.good() && existing.peek() != std::ifstream::traits_type::eof();
    existing.close();

    std::ofstream file(WideToUtf8(AppConfig.defaultPlaylistPath), std::ios::binary | std::ios::app);
    if (!file) return false;
    if (!hasContent) file << "#EXTM3U\n";
    file << "#EXTINF:-1," << TitleFromPath(moviePath) << "\n";
    if (!subtitlePath.empty()) {
        file << "#DLNA-SUBTITLE:" << subtitlePath << "\n";
        file << "#EXTVLCOPT:sub-file=" << subtitlePath << "\n";
    }
    file << moviePath << "\n";
    return true;
}

GtkFileFilter* BuildMediaSourceFilter() {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Media and playlist files");
    const auto extensions = GetMediaSourceFileExtensions();
    for (const auto& ext : extensions) {
        gchar* pattern = g_strdup_printf("*.%s", WideToUtf8(ext).c_str());
        gtk_file_filter_add_pattern(filter, pattern);
        g_free(pattern);
    }
    return filter;
}

GtkFileFilter* BuildSubtitleFilter() {
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Subtitle files");
    const char* patterns[] = {"*.srt", "*.vtt", "*.sub", "*.ass", "*.ssa", "*.smi", "*.txt"};
    for (const char* pattern : patterns) {
        gtk_file_filter_add_pattern(filter, pattern);
    }
    return filter;
}

GtkWidget* g_mainWindow = nullptr;
GtkWidget* g_toolbar = nullptr;
GtkWidget* g_title = nullptr;
GtkWidget* g_addButton = nullptr;
GtkWidget* g_removeButton = nullptr;
GtkWidget* g_startStopButton = nullptr;
GtkWidget* g_settingsButton = nullptr;
GtkWidget* g_status = nullptr;
GtkWidget* g_sourcesScrolled = nullptr;
GtkWidget* g_sources = nullptr;
GtkWidget* g_emptyState = nullptr;

ServerUiState g_state = ServerUiState::Stopped;
std::thread g_worker;
std::thread g_rescanWorker;
std::mutex g_pendingMutex;
ClosePendingState g_closePending;
bool g_hasPendingResult = false;
bool g_pendingSuccess = false;
ServerUiState g_pendingState = ServerUiState::Stopped;
std::string g_pendingMessage;
bool g_sourceListHasFocus = false;
std::atomic<bool> g_scanInProgress{false};

GtkWidget* g_sourceDialog = nullptr;
GtkWidget* g_sourceEntry = nullptr;
GtkWidget* g_sourceAddButton = nullptr;
bool g_sourceDone = false;

GtkWidget* g_playlistDialog = nullptr;
GtkWidget* g_movieEntry = nullptr;
GtkWidget* g_subtitleEntry = nullptr;
bool g_playlistDone = false;

GtkWidget* g_settingsDialog = nullptr;
GtkWidget* g_serverNameEntry = nullptr;
GtkWidget* g_httpPortEntry = nullptr;
GtkWidget* g_ipWhitelistEntry = nullptr;
GtkWidget* g_debugLogCheck = nullptr;
GtkWidget* g_defaultPlaylistCheck = nullptr;
GtkWidget* g_defaultPlaylistAddButton = nullptr;
GtkWidget* g_artistAlbumCheck = nullptr;
GtkWidget* g_hideAllMediaCheck = nullptr;
GtkWidget* g_sortByTitleCheck = nullptr;
GtkWidget* g_flatFoldersCheck = nullptr;
GtkWidget* g_showFileNamesCheck = nullptr;
GtkWidget* g_proxyStreamsCheck = nullptr;
GtkWidget* g_backgroundScanCheck = nullptr;
bool g_settingsSaved = false;
bool g_settingsRestartRequested = false;

GtkWidget* g_logDialog = nullptr;
GtkWidget* g_helpDialog = nullptr;
GtkTextBuffer* g_logBuffer = nullptr;
unsigned long long g_logLastSequence = 0;

// Returns whichever secondary dialog is currently visible, so an
// asynchronously-triggered message box (e.g. a background server
// start/stop/restart completing while Settings or Log is open) becomes
// a CHILD of that dialog instead of a SIBLING of it (both transient-for
// the main window), which is what leaves stacking order ambiguous on
// X11/Wayland window managers that only weakly enforce transient-for
// ordering between siblings. Falls back to g_mainWindow when nothing
// else is open.
GtkWindow* ActiveTopLevelWindow() {
    GtkWidget* candidates[] = { g_settingsDialog, g_logDialog, g_helpDialog, g_playlistDialog, g_sourceDialog };
    for (GtkWidget* candidate : candidates) {
        if (candidate != nullptr && gtk_widget_get_visible(candidate)) {
            return GTK_WINDOW(candidate);
        }
    }
    return GTK_WINDOW(g_mainWindow);
}

GtkWindow* g_activeModal = nullptr;

void PresentModalChild(GtkWindow* child, GtkWindow* parent) {
    if (parent != nullptr) {
        gtk_window_set_transient_for(child, parent);
    }
    gtk_window_set_modal(child, TRUE);
    gtk_window_present(child);
    g_activeModal = child;
}

void ClearActiveModal(GtkWindow* child) {
    if (g_activeModal == child) {
        g_activeModal = nullptr;
    }
}

// hidden debug hook: --dump-widget-geometry builds every Part-1 dialog
// headless, prints [gtk4-<tag>-geometry] rects, and exits before the modal
// loops so no user input is needed. see DumpAllWindowsAndExit.
bool g_dumpGeometry = false;
// hidden debug hook: --dump-log-dialog-reopen exercises the ShowLogDialog
// Close-button-hide then reopen path headless. see DumpLogDialogReopenAndExit.
bool g_dumpLogDialogReopen = false;
// hidden debug hook: --dump-msgbox-parent verifies Task 16's
// ActiveTopLevelWindow parenting of asynchronous failure message boxes.
// see DumpMessageBoxParentAndExit.
bool g_dumpMsgBoxParent = false;

// Number of attempts and delay between attempts when the initial
// stopped distro (microsoft/WSL#11958). Total worst-case wait is
// maxAttempts * delayMs; keep this well under what a user will
// perceive as "the shortcut did nothing" (a few seconds), and well
// above WSLg's typical compositor cold-start time (observed on the
// order of 1-3 seconds in the linked upstream reports).
constexpr int kGuiStartupMaxAttempts = 15;
constexpr int kGuiStartupRetryDelayMs = 300;
void DumpWindowGeometry(const char* tag, GtkWidget* toplevel);
void DumpAllWindowsAndExit(GtkApplication* app);
void DumpLogDialogReopenAndExit(GtkApplication* app);
void DumpMessageBoxParentAndExit(GtkApplication* app);

void RefreshStatus();
void ApplyPendingResult();
void BeginRescan();
void StartServer();
void StopServer();
void RestartServer();
void RequestClose();
void LayoutMainWindow(int width, int height);
void RefreshSourceList();
void RefreshEmptyState();
void RefreshDeleteButton();
void OnSourceFocusChanged(GtkEventControllerFocus*, gboolean, gpointer);
void OnSourceFocusLeave(GtkEventControllerFocus*, gpointer);
void OnSourceSelectionChanged(GtkListBox*, GtkListBoxRow*, gpointer);
bool HasSourceSelection();
void SaveSourcesFromList();
void ShowPlaylistEntryDialog();
void PromptForMediaSource();
void ShowHelpDialog(GtkWindow* parent);
bool ShowSettingsDialog();
void RestoreAndFocusMainWindow();
gboolean OnTrayAction(gpointer);
void OnSingleInstanceCommand(const std::string& cmd);

enum class WindowChrome {
    Main,
    Dialog
};

struct TitlebarState {
    GtkWindow* window;
    GtkWidget* titlebar;
};

static void UpdateTitlebarActiveState(TitlebarState* state) {
    const bool active = gtk_window_is_active(state->window);
    if (active) {
        gtk_widget_add_css_class(state->titlebar, "win10-active");
        gtk_widget_remove_css_class(state->titlebar, "win10-inactive");
    } else {
        gtk_widget_add_css_class(state->titlebar, "win10-inactive");
        gtk_widget_remove_css_class(state->titlebar, "win10-active");
    }
}

static void OnTitlebarActiveNotify(GObject*, GParamSpec*, gpointer userData) {
    UpdateTitlebarActiveState(static_cast<TitlebarState*>(userData));
}

GtkWidget* CreateWin10Titlebar(GtkWindow* window,
                               const char* title,
                               WindowChrome chrome) {
    gtk_window_set_title(window, title);

    GtkWidget* titlebar = gtk_header_bar_new();
    gtk_widget_add_css_class(titlebar, "win10-titlebar");

    GtkWidget* titleLabel = gtk_label_new(title);
    gtk_widget_add_css_class(titleLabel, "title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(titlebar), titleLabel);

    GtkWidget* controls = gtk_window_controls_new(GTK_PACK_END);
    if (chrome == WindowChrome::Main) {
        gtk_window_controls_set_decoration_layout(
            GTK_WINDOW_CONTROLS(controls), ":minimize,close");
    } else {
        gtk_window_controls_set_decoration_layout(
            GTK_WINDOW_CONTROLS(controls), ":close");
    }
    gtk_header_bar_pack_end(GTK_HEADER_BAR(titlebar), controls);

    TitlebarState* state = g_new0(TitlebarState, 1);
    state->window = window;
    state->titlebar = titlebar;
    g_object_set_data_full(G_OBJECT(titlebar), "win10-titlebar-state", state,
                           reinterpret_cast<GDestroyNotify>(g_free));

    UpdateTitlebarActiveState(state);
    g_signal_connect(window, "notify::is-active",
                     G_CALLBACK(OnTitlebarActiveNotify), state);

    return titlebar;
}

GtkWindow* CreateMessageWindow(GtkWindow* parent,
                               const char* title,
                               WindowChrome chrome) {
    GtkWindow* window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_modal(window, TRUE);
    gtk_window_set_resizable(window, FALSE);
    gtk_window_set_transient_for(window, parent);
    gtk_window_set_titlebar(
        window,
        CreateWin10Titlebar(window, title, chrome));
    return window;
}

void MessageBoxShow(GtkWindow* parent, const std::string& text) {
    GtkWindow* msgWin = CreateMessageWindow(parent, "DLNA Server", WindowChrome::Dialog);

    if (g_dumpMsgBoxParent) {
        GtkWindow* transientParent = gtk_window_get_transient_for(msgWin);
        const char* parentTag = "none";
        if (transientParent == GTK_WINDOW(g_settingsDialog)) parentTag = "settings";
        else if (transientParent == GTK_WINDOW(g_logDialog)) parentTag = "log";
        else if (transientParent == GTK_WINDOW(g_playlistDialog)) parentTag = "playlist";
        else if (transientParent == GTK_WINDOW(g_sourceDialog)) parentTag = "source";
        else if (transientParent == GTK_WINDOW(g_mainWindow)) parentTag = "main";
        std::printf("[gtk4-msgbox-parent] parent=%s\n", parentTag);
        gtk_window_destroy(msgWin);
        return;
    }

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_window_set_child(msgWin, vbox);

    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttonBox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), buttonBox);

    gboolean done = FALSE;
    GtkWidget* okButton = gtk_button_new_with_label("OK");
    g_signal_connect(okButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
        *static_cast<gboolean*>(userData) = TRUE;
    }), &done);
    gtk_box_append(GTK_BOX(buttonBox), okButton);

    g_signal_connect(msgWin, "close-request", G_CALLBACK(+[](GtkWidget*, gpointer userData) -> gboolean {
        *static_cast<gboolean*>(userData) = TRUE;
        return TRUE;
    }), &done);

    gtk_window_present(msgWin);
    while (!done) {
        g_main_context_iteration(nullptr, TRUE);
    }
    gtk_window_destroy(msgWin);
}

bool MessageBoxQuestion(GtkWindow* parent, const std::string& text) {
    GtkWindow* msgWin = CreateMessageWindow(parent, "DLNA Server", WindowChrome::Dialog);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox, 16);
    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_window_set_child(msgWin, vbox);

    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttonBox, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), buttonBox);

    struct QuestionState {
        int result;
        gboolean done;
    };
    QuestionState state = { GTK_RESPONSE_NONE, FALSE };

    GtkWidget* yesButton = gtk_button_new_with_label("Yes");
    g_signal_connect(yesButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
        QuestionState* qs = static_cast<QuestionState*>(userData);
        qs->result = GTK_RESPONSE_YES;
        qs->done = TRUE;
    }), &state);
    gtk_box_append(GTK_BOX(buttonBox), yesButton);

    GtkWidget* noButton = gtk_button_new_with_label("No");
    g_signal_connect(noButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer userData) {
        QuestionState* qs = static_cast<QuestionState*>(userData);
        qs->result = GTK_RESPONSE_NO;
        qs->done = TRUE;
    }), &state);
    gtk_box_append(GTK_BOX(buttonBox), noButton);

    g_signal_connect(msgWin, "close-request", G_CALLBACK(+[](GtkWidget*, gpointer userData) -> gboolean {
        QuestionState* qs = static_cast<QuestionState*>(userData);
        qs->result = GTK_RESPONSE_NO;
        qs->done = TRUE;
        return TRUE;
    }), &state);

    gtk_window_present(msgWin);
    while (!state.done) {
        g_main_context_iteration(nullptr, TRUE);
    }
    gtk_window_destroy(msgWin);
    return state.result == GTK_RESPONSE_YES;
}

// Helper functions for file dialogs — kept out of lambdas to avoid
// GCC "embedding a directive within macro arguments" warnings.

void ShowFileChooserNativeImpl(GtkWindow* parent, GtkFileChooserAction action,
                               const char* title, GtkEditable* entry,
                               GtkFileFilter* filter) {
    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(
        title, parent, action, "Select", "Cancel");
    if (filter != nullptr) {
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    }
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
    g_signal_connect(chooser, "response", G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
        if (response == GTK_RESPONSE_ACCEPT) {
            GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
            if (file != nullptr) {
                gchar* path = g_file_get_path(file);
                if (path != nullptr) {
                    gtk_editable_set_text(GTK_EDITABLE(static_cast<GtkEditable*>(data)), path);
                    g_free(path);
                }
                g_object_unref(file);
            }
        }
        gtk_native_dialog_destroy(dialog);
    }), entry);
}

void ShowOpenFileDialog(GtkWindow* parent, const char* title,
                        GtkEditable* entry, GtkFileFilter* filter) {
#if GTK_CHECK_VERSION(4, 10, 0)
    GtkFileDialog* chooser = gtk_file_dialog_new();
    gtk_file_dialog_set_title(chooser, title);
    if (filter != nullptr) {
        GListStore* store = g_list_store_new(GTK_TYPE_FILE_FILTER);
        g_list_store_append(store, filter);
        gtk_file_dialog_set_filters(chooser, G_LIST_MODEL(store));
        g_object_unref(filter);
        g_object_unref(store);
    }
    gtk_file_dialog_open(chooser, parent, nullptr, +[](GObject* source, GAsyncResult* res, gpointer data) {
        GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, nullptr);
        if (file != nullptr) {
            gchar* path = g_file_get_path(file);
            if (path != nullptr) {
                gtk_editable_set_text(GTK_EDITABLE(static_cast<GtkEditable*>(data)), path);
                g_free(path);
            }
            g_object_unref(file);
        }
    }, entry);
    g_object_unref(chooser);
#else
    ShowFileChooserNativeImpl(parent, GTK_FILE_CHOOSER_ACTION_OPEN, title, entry, filter);
#endif
}

void ShowFolderDialog(GtkWindow* parent, const char* title,
                      GtkEditable* entry, GtkFileFilter* filter) {
#if GTK_CHECK_VERSION(4, 10, 0)
    GtkFileDialog* chooser = gtk_file_dialog_new();
    gtk_file_dialog_set_title(chooser, title);
    if (filter != nullptr) {
        GListStore* store = g_list_store_new(GTK_TYPE_FILE_FILTER);
        g_list_store_append(store, filter);
        gtk_file_dialog_set_filters(chooser, G_LIST_MODEL(store));
        g_object_unref(filter);
        g_object_unref(store);
    }
    gtk_file_dialog_select_folder(chooser, parent, nullptr, +[](GObject* source, GAsyncResult* res, gpointer data) {
        GFile* file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), res, nullptr);
        if (file != nullptr) {
            gchar* path = g_file_get_path(file);
            if (path != nullptr) {
                gtk_editable_set_text(GTK_EDITABLE(static_cast<GtkEditable*>(data)), path);
                g_free(path);
            }
            g_object_unref(file);
        }
    }, entry);
    g_object_unref(chooser);
#else
    ShowFileChooserNativeImpl(parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, title, entry, filter);
#endif
}

void ShowPlaylistMovieBrowse(GtkWindow* parent) {
    ShowOpenFileDialog(parent, "Choose movie file",
                       GTK_EDITABLE(g_movieEntry), BuildMediaSourceFilter());
}

void ShowPlaylistSubtitleBrowse(GtkWindow* parent) {
    ShowOpenFileDialog(parent, "Choose subtitle file",
                       GTK_EDITABLE(g_subtitleEntry), BuildSubtitleFilter());
}

void ShowPlaylistEntryDialog() {
    if (g_playlistDialog != nullptr) return;
    g_playlistDone = false;

    g_playlistDialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(g_playlistDialog), "Default playlist entry");
    gtk_window_set_default_size(GTK_WINDOW(g_playlistDialog),
                                UiTokens::kPlaylistWindowWidth, UiTokens::kPlaylistWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(g_playlistDialog), FALSE);

    gtk_window_set_titlebar(
        GTK_WINDOW(g_playlistDialog),
        CreateWin10Titlebar(GTK_WINDOW(g_playlistDialog),
                            "Default playlist entry",
                            WindowChrome::Dialog));

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokens::kPlaylistWindowWidth, UiTokens::kPlaylistWindowHeight);
    gtk_window_set_child(GTK_WINDOW(g_playlistDialog), fixed);

    GtkWidget* movieLabel = gtk_label_new("Movie path:");
    gtk_widget_set_size_request(movieLabel, UiTokens::kPlaylistMovieLabelW, UiTokens::kPlaylistMovieLabelH);
    gtk_fixed_put(GTK_FIXED(fixed), movieLabel, UiTokens::kPlaylistMovieLabelX, UiTokens::kPlaylistMovieLabelY);
    gtk_label_set_xalign(GTK_LABEL(movieLabel), 0.0f);

    g_movieEntry = gtk_entry_new();
    gtk_widget_set_size_request(g_movieEntry, UiTokens::kPlaylistMovieEditW, UiTokens::kPlaylistMovieEditH);
    gtk_fixed_put(GTK_FIXED(fixed), g_movieEntry, UiTokens::kPlaylistMovieEditX, UiTokens::kPlaylistMovieEditY);

    GtkWidget* movieBrowse = gtk_button_new_with_label("Browse...");
    gtk_widget_set_size_request(movieBrowse, UiTokens::kPlaylistMovieBrowseW, UiTokens::kPlaylistMovieBrowseH);
    gtk_fixed_put(GTK_FIXED(fixed), movieBrowse, UiTokens::kPlaylistMovieBrowseX, UiTokens::kPlaylistMovieBrowseY);
    gtk_widget_add_css_class(movieBrowse, "toolbar-button");
    g_signal_connect(movieBrowse, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ShowPlaylistMovieBrowse(GTK_WINDOW(g_playlistDialog));
    }), nullptr);

    GtkWidget* subtitleLabel = gtk_label_new("Subtitle path:");
    gtk_widget_set_size_request(subtitleLabel, UiTokens::kPlaylistSubtitleLabelW, UiTokens::kPlaylistSubtitleLabelH);
    gtk_fixed_put(GTK_FIXED(fixed), subtitleLabel, UiTokens::kPlaylistSubtitleLabelX, UiTokens::kPlaylistSubtitleLabelY);
    gtk_label_set_xalign(GTK_LABEL(subtitleLabel), 0.0f);

    g_subtitleEntry = gtk_entry_new();
    gtk_widget_set_size_request(g_subtitleEntry, UiTokens::kPlaylistSubtitleEditW, UiTokens::kPlaylistSubtitleEditH);
    gtk_fixed_put(GTK_FIXED(fixed), g_subtitleEntry, UiTokens::kPlaylistSubtitleEditX, UiTokens::kPlaylistSubtitleEditY);

    GtkWidget* subtitleBrowse = gtk_button_new_with_label("Browse...");
    gtk_widget_set_size_request(subtitleBrowse, UiTokens::kPlaylistSubtitleBrowseW, UiTokens::kPlaylistSubtitleBrowseH);
    gtk_fixed_put(GTK_FIXED(fixed), subtitleBrowse, UiTokens::kPlaylistSubtitleBrowseX, UiTokens::kPlaylistSubtitleBrowseY);
    gtk_widget_add_css_class(subtitleBrowse, "toolbar-button");
    g_signal_connect(subtitleBrowse, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ShowPlaylistSubtitleBrowse(GTK_WINDOW(g_playlistDialog));
    }), nullptr);

    GtkWidget* addButton = gtk_button_new_with_label("Add");
    gtk_widget_set_size_request(addButton, UiTokens::kPlaylistAddW, UiTokens::kPlaylistAddH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), addButton, UiTokens::kPlaylistAddX, UiTokens::kPlaylistAddY);
    gtk_widget_add_css_class(addButton, "toolbar-button");
    gtk_widget_add_css_class(addButton, "suggested-action");
    g_signal_connect(addButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_playlistDone = true;
        gtk_widget_set_visible(g_playlistDialog, FALSE);
    }), nullptr);

    g_signal_connect(g_playlistDialog, "close-request", G_CALLBACK(+[](GtkWidget* w, gpointer) -> gboolean {
        g_playlistDone = true;
        gtk_widget_set_visible(w, FALSE);
        ClearActiveModal(GTK_WINDOW(w));
        return TRUE;
    }), nullptr);

    g_signal_connect(g_playlistDialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_playlistDialog = nullptr;
    }), nullptr);

    if (g_dumpGeometry) {
        PresentModalChild(GTK_WINDOW(g_playlistDialog), GTK_WINDOW(g_mainWindow));
        DumpWindowGeometry("playlist-entry", g_playlistDialog);
        return;
    }
    PresentModalChild(GTK_WINDOW(g_playlistDialog), GTK_WINDOW(g_mainWindow));

    while (!g_playlistDone) {
        g_main_context_iteration(nullptr, TRUE);
    }
    gtk_widget_set_visible(g_playlistDialog, FALSE);
    ClearActiveModal(GTK_WINDOW(g_playlistDialog));

    if (g_playlistDone) {
        const std::string movie = gtk_editable_get_text(GTK_EDITABLE(g_movieEntry));
        const std::string subtitle = gtk_editable_get_text(GTK_EDITABLE(g_subtitleEntry));
        if (!movie.empty()) {
            if (!AppendDefaultPlaylistEntry(movie, subtitle)) {
                MessageBoxShow(GTK_WINDOW(g_mainWindow), "Could not write default playlist.");
            } else {
                AppConfig.defaultPlaylistEnabled = true;
                AppConfig.Save();
            }
        }
    }
    gtk_window_destroy(GTK_WINDOW(g_playlistDialog));
}

void OnSourceEntryChanged(GtkEditable* editable, gpointer) {
    const gchar* text = gtk_editable_get_text(editable);
    gtk_widget_set_sensitive(g_sourceAddButton,
                             AnyFieldHasContent({static_cast<int>(g_utf8_strlen(text, -1))}));
}

void ChooseSourcePath(GtkWindow* parent, GtkFileChooserAction action, const char* title) {
    if (action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) {
        ShowFolderDialog(parent, title, GTK_EDITABLE(g_sourceEntry), nullptr);
    } else {
        ShowOpenFileDialog(parent, title, GTK_EDITABLE(g_sourceEntry), BuildMediaSourceFilter());
    }
}

void PromptForMediaSource() {
    if (g_sourceDialog != nullptr) return;
    g_sourceDone = false;

    g_sourceDialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(g_sourceDialog), "Add media source");
    gtk_window_set_default_size(GTK_WINDOW(g_sourceDialog),
                                UiTokens::kSourcePromptWindowWidth, UiTokens::kSourcePromptWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(g_sourceDialog), FALSE);

    gtk_window_set_titlebar(
        GTK_WINDOW(g_sourceDialog),
        CreateWin10Titlebar(GTK_WINDOW(g_sourceDialog),
                            "Add media source",
                            WindowChrome::Dialog));

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokens::kSourcePromptWindowWidth, UiTokens::kSourcePromptWindowHeight);
    gtk_window_set_child(GTK_WINDOW(g_sourceDialog), fixed);

    GtkWidget* label = gtk_label_new("Add a local source or a Network share URL:");
    gtk_widget_set_size_request(label, UiTokens::kSourcePromptLabelW, UiTokens::kSourcePromptLabelH);
    gtk_fixed_put(GTK_FIXED(fixed), label, UiTokens::kSourcePromptLabelX, UiTokens::kSourcePromptLabelY);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

    g_sourceEntry = gtk_entry_new();
    gtk_widget_set_size_request(g_sourceEntry, UiTokens::kSourcePromptEditW, UiTokens::kSourcePromptEditH);
    gtk_fixed_put(GTK_FIXED(fixed), g_sourceEntry, UiTokens::kSourcePromptEditX, UiTokens::kSourcePromptEditY);
    g_signal_connect(g_sourceEntry, "changed", G_CALLBACK(OnSourceEntryChanged), nullptr);

    GtkWidget* hintLabel = gtk_label_new("Example: ftp://user:pass@server:21/media");
    gtk_widget_set_size_request(hintLabel, UiTokens::kSourcePromptHintW, UiTokens::kSourcePromptHintH);
    gtk_fixed_put(GTK_FIXED(fixed), hintLabel, UiTokens::kSourcePromptHintX, UiTokens::kSourcePromptHintY);
    gtk_label_set_xalign(GTK_LABEL(hintLabel), 0.0f);

    GtkWidget* folderButton = gtk_button_new_with_label("Folder...");
    gtk_widget_set_size_request(folderButton, UiTokens::kSourcePromptFolderW, UiTokens::kSourcePromptFolderH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), folderButton, UiTokens::kSourcePromptFolderX, UiTokens::kSourcePromptFolderY);
    gtk_widget_add_css_class(folderButton, "toolbar-button");
    g_signal_connect(folderButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ChooseSourcePath(GTK_WINDOW(g_sourceDialog), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                         "Choose media folder");
    }), nullptr);

    GtkWidget* fileButton = gtk_button_new_with_label("File...");
    gtk_widget_set_size_request(fileButton, UiTokens::kSourcePromptFileW, UiTokens::kSourcePromptFileH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), fileButton, UiTokens::kSourcePromptFileX, UiTokens::kSourcePromptFileY);
    gtk_widget_add_css_class(fileButton, "toolbar-button");
    g_signal_connect(fileButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ChooseSourcePath(GTK_WINDOW(g_sourceDialog), GTK_FILE_CHOOSER_ACTION_OPEN,
                         "Choose media file");
    }), nullptr);

    g_sourceAddButton = gtk_button_new_with_label("Add");
    gtk_widget_set_size_request(g_sourceAddButton, UiTokens::kSourcePromptAddW, UiTokens::kSourcePromptAddH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), g_sourceAddButton, UiTokens::kSourcePromptAddX, UiTokens::kSourcePromptAddY);
    gtk_widget_add_css_class(g_sourceAddButton, "toolbar-button");
    gtk_widget_add_css_class(g_sourceAddButton, "suggested-action");
    gtk_widget_set_sensitive(g_sourceAddButton, FALSE);
    g_signal_connect(g_sourceAddButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_sourceDone = true;
        gtk_widget_hide(g_sourceDialog);
    }), nullptr);

    GtkWidget* cancelButton = gtk_button_new_with_label("Cancel");
    gtk_widget_set_size_request(cancelButton, UiTokens::kSourcePromptCancelW, UiTokens::kSourcePromptCancelH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), cancelButton, UiTokens::kSourcePromptCancelX, UiTokens::kSourcePromptCancelY);
    gtk_widget_add_css_class(cancelButton, "toolbar-button");
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        gtk_editable_set_text(GTK_EDITABLE(g_sourceEntry), "");
        g_sourceDone = false;
        gtk_widget_set_visible(g_sourceDialog, FALSE);
        ClearActiveModal(GTK_WINDOW(g_sourceDialog));
    }), nullptr);

    g_signal_connect(g_sourceDialog, "close-request", G_CALLBACK(+[](GtkWidget* w, gpointer) -> gboolean {
        g_sourceDone = false;
        gtk_widget_set_visible(w, FALSE);
        ClearActiveModal(GTK_WINDOW(w));
        return TRUE;
    }), nullptr);

    g_signal_connect(g_sourceDialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_sourceDialog = nullptr;
    }), nullptr);

    if (g_dumpGeometry) {
        PresentModalChild(GTK_WINDOW(g_sourceDialog), GTK_WINDOW(g_mainWindow));
        DumpWindowGeometry("source-prompt", g_sourceDialog);
        return;
    }
    PresentModalChild(GTK_WINDOW(g_sourceDialog), GTK_WINDOW(g_mainWindow));

    while (gtk_widget_get_visible(g_sourceDialog)) {
        g_main_context_iteration(nullptr, TRUE);
    }
    ClearActiveModal(GTK_WINDOW(g_sourceDialog));

    if (g_sourceDone) {
        const gchar* text = gtk_editable_get_text(GTK_EDITABLE(g_sourceEntry));
        if (text != nullptr && *text != '\0') {
            const std::string selected = text;
            bool duplicate = false;
            GtkWidget* rowWidget = gtk_widget_get_first_child(g_sources);
            while (rowWidget != nullptr) {
                GtkWidget* rowChild = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(rowWidget));
                if (GTK_IS_LABEL(rowChild)) {
                    const gchar* rowText = gtk_label_get_text(GTK_LABEL(rowChild));
                    if (rowText != nullptr && selected == rowText) {
                        duplicate = true;
                        break;
                    }
                }
                rowWidget = gtk_widget_get_next_sibling(rowWidget);
            }
            if (!duplicate) {
                GtkWidget* row = gtk_list_box_row_new();
                GtkWidget* rowLabel = gtk_label_new(selected.c_str());
                gtk_label_set_xalign(GTK_LABEL(rowLabel), 0.0f);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rowLabel);
                gtk_list_box_append(GTK_LIST_BOX(g_sources), row);
                SaveSourcesFromList();
            }
        }
        g_sourceDone = false;
    }
    gtk_window_destroy(GTK_WINDOW(g_sourceDialog));
}

void LoadLogInitial() {
    LogSnapshot snapshot = GetSystemLogSince(0);
    g_logLastSequence = snapshot.latestSequence;
    if (!snapshot.text.empty()) {
        gtk_text_buffer_set_text(g_logBuffer, WideToUtf8(snapshot.text).c_str(), -1);
    }
    GtkTextIter endIter;
    gtk_text_buffer_get_end_iter(g_logBuffer, &endIter);
    gtk_text_buffer_place_cursor(g_logBuffer, &endIter);
}

void AppendLogSinceLast() {
    LogSnapshot snapshot = GetSystemLogSince(g_logLastSequence);
    g_logLastSequence = snapshot.latestSequence;
    if (snapshot.text.empty()) return;
    GtkTextIter endIter;
    gtk_text_buffer_get_end_iter(g_logBuffer, &endIter);
    gtk_text_buffer_insert(g_logBuffer, &endIter, WideToUtf8(snapshot.text).c_str(), -1);
}

void RefreshLogDialog() {
    LoadLogInitial();
}

void ShowLogDialog() {
    if (g_logDialog != nullptr) {
        gtk_window_present(GTK_WINDOW(g_logDialog));
        return;
    }

    g_logDialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(g_logDialog), "DLNA Server Log");
    gtk_window_set_default_size(GTK_WINDOW(g_logDialog),
                                UiTokens::kLogWindowWidth, UiTokens::kLogWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(g_logDialog), FALSE);

    gtk_window_set_titlebar(
        GTK_WINDOW(g_logDialog),
        CreateWin10Titlebar(GTK_WINDOW(g_logDialog),
                            "DLNA Server Log",
                            WindowChrome::Dialog));

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokens::kLogWindowWidth, UiTokens::kLogWindowHeight);
    gtk_window_set_child(GTK_WINDOW(g_logDialog), fixed);

    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolled, UiTokens::kLogTextW, UiTokens::kLogTextH);
    gtk_fixed_put(GTK_FIXED(fixed), scrolled, UiTokens::kLogTextX, UiTokens::kLogTextY);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* textView = gtk_text_view_new();
    g_logBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textView));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textView), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textView), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), textView);

    GtkWidget* refreshButton = gtk_button_new_with_label("Refresh");
    gtk_widget_set_size_request(refreshButton, UiTokens::kLogRefreshW, UiTokens::kLogRefreshH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), refreshButton, UiTokens::kLogRefreshX, UiTokens::kLogRefreshY);
    gtk_widget_add_css_class(refreshButton, "toolbar-button");
    g_signal_connect(refreshButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        RefreshLogDialog();
    }), nullptr);

    GtkWidget* closeButton = gtk_button_new_with_label("Close");
    gtk_widget_set_size_request(closeButton, UiTokens::kLogCloseW, UiTokens::kLogCloseH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), closeButton, UiTokens::kLogCloseX, UiTokens::kLogCloseY);
    gtk_widget_add_css_class(closeButton, "toolbar-button");
    g_signal_connect(closeButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        gtk_widget_set_visible(g_logDialog, FALSE);
        ClearActiveModal(GTK_WINDOW(g_logDialog));
        if (g_mainWindow != nullptr) {
            gtk_window_present(GTK_WINDOW(g_mainWindow));
        }
    }), nullptr);

    g_signal_connect(g_logDialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_logDialog = nullptr;
    }), nullptr);

    g_signal_connect(g_logDialog, "close-request", G_CALLBACK(+[](GtkWidget*, gpointer) {
        gtk_widget_set_visible(g_logDialog, FALSE);
        ClearActiveModal(GTK_WINDOW(g_logDialog));
        return TRUE;
    }), nullptr);

    RefreshLogDialog();
    if (g_dumpGeometry) {
        PresentModalChild(GTK_WINDOW(g_logDialog), GTK_WINDOW(g_mainWindow));
        DumpWindowGeometry("log", g_logDialog);
        return;
    }
    if (g_dumpLogDialogReopen) {
        // headless reopen test: present without the modal loop so
        // DumpLogDialogReopenAndExit can drive the hide/reopen sequence
        PresentModalChild(GTK_WINDOW(g_logDialog), GTK_WINDOW(g_mainWindow));
        return;
    }
    PresentModalChild(GTK_WINDOW(g_logDialog), GTK_WINDOW(g_mainWindow));
    while (gtk_widget_get_visible(g_logDialog)) {
        g_main_context_iteration(nullptr, TRUE);
    }
    ClearActiveModal(GTK_WINDOW(g_logDialog));
}

void ShowHelpDialog(GtkWindow* parent) {
    if (g_helpDialog != nullptr) {
        gtk_window_present(GTK_WINDOW(g_helpDialog));
        return;
    }
    GtkWidget* dialog = gtk_window_new();
    g_helpDialog = dialog;
    gtk_window_set_title(GTK_WINDOW(dialog), "DLNA Server Help");
    gtk_window_set_default_size(GTK_WINDOW(dialog),
                                UiTokens::kHelpWindowWidth, UiTokens::kHelpWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    gtk_window_set_titlebar(
        GTK_WINDOW(dialog),
        CreateWin10Titlebar(GTK_WINDOW(dialog),
                            "DLNA Server Help",
                            WindowChrome::Dialog));

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokens::kHelpWindowWidth, UiTokens::kHelpWindowHeight);
    gtk_window_set_child(GTK_WINDOW(dialog), fixed);

    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scrolled, UiTokens::kHelpTextW, UiTokens::kHelpTextH);
    gtk_fixed_put(GTK_FIXED(fixed), scrolled, UiTokens::kHelpTextX, UiTokens::kHelpTextY);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* textView = gtk_text_view_new();
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textView));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textView), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textView), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), textView);

    // measure the longest flag or setting label to size the tab stop
    // mirror the win32 columnGutterPx and longestLabelPx computation
    PangoLayout* measureLayout = gtk_widget_create_pango_layout(textView, nullptr);
    int longestLabelPx = 0;
    const auto measure = [&measureLayout, &longestLabelPx](const std::wstring& label) {
        pango_layout_set_text(measureLayout, WideToUtf8(label).c_str(), -1);
        int labelWidth = 0;
        int labelHeight = 0;
        pango_layout_get_pixel_size(measureLayout, &labelWidth, &labelHeight);
        if (labelWidth > longestLabelPx) longestLabelPx = labelWidth;
    };
    for (const auto& flag : GetCliFlagTable()) measure(flag.flag);
    for (const auto& setting : GetSettingsHelpTable()) measure(setting.label);
    g_object_unref(measureLayout);

    const int columnGutterPx = 24;
    const int tabStopPx = longestLabelPx + columnGutterPx;

    // tab stops are expressed in pango units when positionsInPixels is
    // false which matches pango_layout_get_pixel_size measurements
    PangoTabArray* tabArray = pango_tab_array_new(1, FALSE);
    pango_tab_array_set_tab(tabArray, 0, PANGO_TAB_LEFT, tabStopPx * PANGO_SCALE);
    gtk_text_view_set_tabs(GTK_TEXT_VIEW(textView), tabArray);
    pango_tab_array_free(tabArray);

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);

    GtkTextTag* boldTag = gtk_text_buffer_create_tag(buffer, nullptr, "weight", PANGO_WEIGHT_BOLD, nullptr);

    auto insertHeader = [&](const std::wstring& header) {
        gtk_text_buffer_insert_with_tags(buffer, &iter, WideToUtf8(header).c_str(), -1, boldTag, nullptr);
        gtk_text_buffer_insert(buffer, &iter, "\n\n", -1);
    };
    auto insertRow = [&](const std::wstring& flag, const std::wstring& meaning) {
        std::string line = WideToUtf8(flag) + "\t" + WideToUtf8(meaning) + "\n";
        gtk_text_buffer_insert(buffer, &iter, line.c_str(), -1);
    };

    insertHeader(L"Command-Line Flags");
    for (const auto& flag : GetCliFlagTable()) insertRow(flag.flag, flag.meaning);
    gtk_text_buffer_insert(buffer, &iter, "\n", -1);
    insertHeader(L"Settings");
    for (const auto& setting : GetSettingsHelpTable()) insertRow(setting.label, setting.meaning);

    g_signal_connect(dialog, "close-request", G_CALLBACK(+[](GtkWidget* w, gpointer) -> gboolean {
        gtk_widget_set_visible(w, FALSE);
        g_helpDialog = nullptr;
        ClearActiveModal(GTK_WINDOW(w));
        return TRUE;
    }), nullptr);

    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_helpDialog = nullptr;
    }), nullptr);

    if (g_dumpGeometry) {
        PresentModalChild(GTK_WINDOW(dialog), parent ? parent : GTK_WINDOW(g_mainWindow));
        DumpWindowGeometry("help", dialog);
        return;
    }
    PresentModalChild(GTK_WINDOW(dialog), parent ? parent : GTK_WINDOW(g_mainWindow));
    while (gtk_widget_get_visible(dialog)) {
        g_main_context_iteration(nullptr, TRUE);
    }
    ClearActiveModal(GTK_WINDOW(dialog));
}

void RefreshDefaultPlaylistControls() {
    gtk_widget_set_sensitive(g_defaultPlaylistAddButton,
                             gtk_check_button_get_active(GTK_CHECK_BUTTON(g_defaultPlaylistCheck)));
}

void LoadSettingsFromConfig() {
    const ConfigSnapshot cfg = AppConfig.Snapshot();
    gtk_editable_set_text(GTK_EDITABLE(g_serverNameEntry), ToUtf8(cfg.serverName).c_str());
    gtk_editable_set_text(GTK_EDITABLE(g_httpPortEntry), std::to_string(cfg.port).c_str());
    gtk_editable_set_text(GTK_EDITABLE(g_ipWhitelistEntry), ToUtf8(cfg.ipWhiteList).c_str());
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_debugLogCheck), cfg.debugLog);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_defaultPlaylistCheck), cfg.defaultPlaylistEnabled);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_artistAlbumCheck), cfg.addArtistAlbumFolders);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_hideAllMediaCheck), cfg.doNotShowAllMediaFolders);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_flatFoldersCheck), cfg.flatFolderStyle);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_showFileNamesCheck), cfg.showFileNamesInsteadOfTitles);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_sortByTitleCheck), cfg.sortByTitle);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_proxyStreamsCheck), cfg.proxyStreams);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(g_backgroundScanCheck), cfg.backgroundScanEnabled);
    RefreshDefaultPlaylistControls();
}

bool SaveSettingsToConfig() {
    int httpPort = 0;
    const gchar* portText = gtk_editable_get_text(GTK_EDITABLE(g_httpPortEntry));
    if (!TryParsePortStrict(portText ? portText : "", httpPort)) {
        MessageBoxShow(GTK_WINDOW(g_settingsDialog), "HTTP port must be between 1 and 65535.");
        return false;
    }
    const std::wstring serverName = ToWideString(gtk_editable_get_text(GTK_EDITABLE(g_serverNameEntry)));
    const std::wstring ipWhiteList = ToWideString(gtk_editable_get_text(GTK_EDITABLE(g_ipWhitelistEntry)));
    const bool debugLog = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_debugLogCheck));
    const bool defaultPlaylistEnabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_defaultPlaylistCheck));
    const bool addArtistAlbumFolders = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_artistAlbumCheck));
    const bool doNotShowAllMediaFolders = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_hideAllMediaCheck));
    const bool flatFolderStyle = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_flatFoldersCheck));
    const bool showFileNamesInsteadOfTitles = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_showFileNamesCheck));
    const bool sortByTitle = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_sortByTitleCheck));
    const bool proxyStreams = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_proxyStreamsCheck));
    const bool backgroundScanEnabled = gtk_check_button_get_active(GTK_CHECK_BUTTON(g_backgroundScanCheck));

    const ConfigSnapshot before = AppConfig.Snapshot();
    AppConfig.Mutate([&](Config& cfg) {
        cfg.serverName = serverName;
        cfg.port = httpPort;
        cfg.ipWhiteList = ipWhiteList;
        cfg.debugLog = debugLog;
        cfg.defaultPlaylistEnabled = defaultPlaylistEnabled;
        if (cfg.defaultPlaylistPath.empty()) cfg.defaultPlaylistPath = cfg.GetDefaultPlaylistPath();
        cfg.addArtistAlbumFolders = addArtistAlbumFolders;
        cfg.doNotShowAllMediaFolders = doNotShowAllMediaFolders;
        cfg.flatFolderStyle = flatFolderStyle;
        cfg.showFileNamesInsteadOfTitles = showFileNamesInsteadOfTitles;
        cfg.sortByTitle = sortByTitle;
        cfg.proxyStreams = proxyStreams;
        cfg.backgroundScanEnabled = backgroundScanEnabled;
    });
    AppConfig.Save();
    LogPrint(L"Saved settings.");

    g_settingsRestartRequested = false;
    if (DLNAServer.IsRunning()) {
        const ConfigSnapshot after = AppConfig.Snapshot();
        std::vector<std::wstring> changed = DetermineSettingsRequiringRestart(before, after);
        if (!changed.empty()) {
            std::wstring names;
            for (size_t i = 0; i < changed.size(); ++i) {
                if (i) names += L", ";
                names += changed[i];
            }
            std::string prompt = "A server restart is needed to apply changes to: " +
                                 ToUtf8(names) + ".\n\nRestart server?";
            g_settingsRestartRequested =
                MessageBoxQuestion(GTK_WINDOW(g_settingsDialog), prompt);
        }
    }
    return true;
}

bool ShowSettingsDialog() {
    if (g_settingsDialog != nullptr) return false;
    g_settingsSaved = false;
    g_settingsRestartRequested = false;

    g_settingsDialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(g_settingsDialog), "DLNA Server Settings");
    gtk_window_set_default_size(GTK_WINDOW(g_settingsDialog),
                                UiTokens::kSettingsWindowWidth, UiTokens::kSettingsWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(g_settingsDialog), FALSE);

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokens::kSettingsWindowWidth, UiTokens::kSettingsWindowHeight);
    gtk_window_set_child(GTK_WINDOW(g_settingsDialog), fixed);

    auto makeFrame = [&](const char* label, int x, int y, int w, int h) {
        GtkWidget* frame = gtk_frame_new(label);
        gtk_widget_set_size_request(frame, w, h);
        gtk_fixed_put(GTK_FIXED(fixed), frame, x, y);
        gtk_frame_set_label_align(GTK_FRAME(frame), 0.0f);
        return frame;
    };
    auto makeLabel = [&](const char* text, int x, int y, int w, int h) {
        GtkWidget* widget = gtk_label_new(text);
        gtk_widget_set_size_request(widget, w, h);
        gtk_fixed_put(GTK_FIXED(fixed), widget, x, y);
        gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
        return widget;
    };
    auto makeEntry = [&](int x, int y, int w, int h) {
        GtkWidget* entry = gtk_entry_new();
        gtk_widget_set_size_request(entry, w, h);
        gtk_fixed_put(GTK_FIXED(fixed), entry, x, y);
        return entry;
    };
    auto makeCheck = [&](const char* text, int x, int y, int w, int h) {
        GtkWidget* check = gtk_check_button_new_with_label(text);
        gtk_widget_set_size_request(check, w, h);
        gtk_fixed_put(GTK_FIXED(fixed), check, x, y);
        return check;
    };

    // menu bar row mirrors the win32 SetMenu Logs/Help bar that sits in
    // the non-client area so it is absent from the geometry dump and the
    // group boxes below start at the captured y coordinate of 21
    const int kSettingsToolbarButtonWidth = 72;
    const int kSettingsToolbarButtonHeight = 28;
    const int kSettingsToolbarTop = UiTokens::kSettingsServerGroupY - 36;

    GtkWidget* logsButton = gtk_button_new_with_label("Logs");
    gtk_widget_set_size_request(logsButton, kSettingsToolbarButtonWidth, kSettingsToolbarButtonHeight);
    gtk_fixed_put(GTK_FIXED(fixed), logsButton, UiTokens::kSettingsServerGroupX,
                  kSettingsToolbarTop);
    gtk_widget_add_css_class(logsButton, "flat");
    g_signal_connect(logsButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ShowLogDialog();
    }), nullptr);

    GtkWidget* helpButton = gtk_button_new_with_label("Help");
    gtk_widget_set_size_request(helpButton, kSettingsToolbarButtonWidth, kSettingsToolbarButtonHeight);
    gtk_fixed_put(GTK_FIXED(fixed), helpButton,
                  UiTokens::kSettingsServerGroupX + kSettingsToolbarButtonWidth + UiTokens::kButtonGap,
                  kSettingsToolbarTop);
    gtk_widget_add_css_class(helpButton, "flat");
    g_signal_connect(helpButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ShowHelpDialog(GTK_WINDOW(g_settingsDialog));
    }), nullptr);

    makeFrame("Server", UiTokens::kSettingsServerGroupX, UiTokens::kSettingsServerGroupY,
              UiTokens::kSettingsServerGroupW, UiTokens::kSettingsServerGroupH);
    makeLabel("Server name:", UiTokens::kSettingsServerNameLabelX, UiTokens::kSettingsServerNameLabelY,
              UiTokens::kSettingsServerNameLabelW, UiTokens::kSettingsServerNameLabelH);
    g_serverNameEntry = makeEntry(UiTokens::kSettingsServerNameEditX, UiTokens::kSettingsServerNameEditY,
                                  UiTokens::kSettingsServerNameEditW, UiTokens::kSettingsServerNameEditH);
    makeLabel("HTTP port:", UiTokens::kSettingsHttpPortLabelX, UiTokens::kSettingsHttpPortLabelY,
              UiTokens::kSettingsHttpPortLabelW, UiTokens::kSettingsHttpPortLabelH);
    g_httpPortEntry = makeEntry(UiTokens::kSettingsHttpPortEditX, UiTokens::kSettingsHttpPortEditY,
                                UiTokens::kSettingsHttpPortEditW, UiTokens::kSettingsHttpPortEditH);
    makeLabel("IP whitelist:", UiTokens::kSettingsIpWhitelistLabelX, UiTokens::kSettingsIpWhitelistLabelY,
              UiTokens::kSettingsIpWhitelistLabelW, UiTokens::kSettingsIpWhitelistLabelH);
    g_ipWhitelistEntry = makeEntry(UiTokens::kSettingsIpWhitelistEditX, UiTokens::kSettingsIpWhitelistEditY,
                                   UiTokens::kSettingsIpWhitelistEditW, UiTokens::kSettingsIpWhitelistEditH);

    makeFrame("General", UiTokens::kSettingsGeneralGroupX, UiTokens::kSettingsGeneralGroupY,
              UiTokens::kSettingsGeneralGroupW, UiTokens::kSettingsGeneralGroupH);
    g_debugLogCheck = makeCheck("Debug log (write to file)",
                                UiTokens::kSettingsDebugLogX, UiTokens::kSettingsDebugLogY,
                                UiTokens::kSettingsDebugLogW, UiTokens::kSettingsDebugLogH);

    makeFrame("Playlist", UiTokens::kSettingsPlaylistGroupX, UiTokens::kSettingsPlaylistGroupY,
              UiTokens::kSettingsPlaylistGroupW, UiTokens::kSettingsPlaylistGroupH);
    g_defaultPlaylistCheck = makeCheck("Default playlist",
                                       UiTokens::kSettingsDefaultPlaylistX, UiTokens::kSettingsDefaultPlaylistY,
                                       UiTokens::kSettingsDefaultPlaylistW, UiTokens::kSettingsDefaultPlaylistH);
    g_defaultPlaylistAddButton = gtk_button_new_with_label("Add...");
    gtk_widget_set_size_request(g_defaultPlaylistAddButton,
                                  UiTokens::kSettingsPlaylistAddW, UiTokens::kSettingsPlaylistAddH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), g_defaultPlaylistAddButton,
                  UiTokens::kSettingsPlaylistAddX, UiTokens::kSettingsPlaylistAddY);
    gtk_widget_add_css_class(g_defaultPlaylistAddButton, "toolbar-button");
    g_signal_connect(g_defaultPlaylistAddButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        ShowPlaylistEntryDialog();
        gtk_check_button_set_active(GTK_CHECK_BUTTON(g_defaultPlaylistCheck), TRUE);
        RefreshDefaultPlaylistControls();
    }), nullptr);
    g_signal_connect(g_defaultPlaylistCheck, "toggled", G_CALLBACK(+[](GtkWidget*, gpointer) {
        RefreshDefaultPlaylistControls();
    }), nullptr);

    makeFrame("Media browsing", UiTokens::kSettingsMediaGroupX, UiTokens::kSettingsMediaGroupY,
              UiTokens::kSettingsMediaGroupW, UiTokens::kSettingsMediaGroupH);
    g_artistAlbumCheck = makeCheck("Add artist/album folders to audio",
                                   UiTokens::kSettingsArtistAlbumsX, UiTokens::kSettingsArtistAlbumsY,
                                   UiTokens::kSettingsArtistAlbumsW, UiTokens::kSettingsArtistAlbumsH);
    g_hideAllMediaCheck = makeCheck("Do not show 'All Media' folders",
                                    UiTokens::kSettingsHideAllMediaX, UiTokens::kSettingsHideAllMediaY,
                                    UiTokens::kSettingsHideAllMediaW, UiTokens::kSettingsHideAllMediaH);
    g_sortByTitleCheck = makeCheck("Sort by title instead of file name",
                                   UiTokens::kSettingsSortByTitleX, UiTokens::kSettingsSortByTitleY,
                                   UiTokens::kSettingsSortByTitleW, UiTokens::kSettingsSortByTitleH);
    g_flatFoldersCheck = makeCheck("Flat folders style",
                                   UiTokens::kSettingsFlatFoldersX, UiTokens::kSettingsFlatFoldersY,
                                   UiTokens::kSettingsFlatFoldersW, UiTokens::kSettingsFlatFoldersH);
    g_showFileNamesCheck = makeCheck("Show file names instead of titles",
                                     UiTokens::kSettingsShowFileNamesX, UiTokens::kSettingsShowFileNamesY,
                                     UiTokens::kSettingsShowFileNamesW, UiTokens::kSettingsShowFileNamesH);
    g_proxyStreamsCheck = makeCheck("Proxy streams",
                                    UiTokens::kSettingsProxyStreamsX, UiTokens::kSettingsProxyStreamsY,
                                    UiTokens::kSettingsProxyStreamsW, UiTokens::kSettingsProxyStreamsH);
    g_backgroundScanCheck = makeCheck("Background scan (auto-rescan on changes)",
                                      UiTokens::kSettingsBackgroundScanX, UiTokens::kSettingsBackgroundScanY,
                                      UiTokens::kSettingsBackgroundScanW, UiTokens::kSettingsBackgroundScanH);

    GtkWidget* cancelButton = gtk_button_new_with_label("Cancel");
    gtk_widget_set_size_request(cancelButton, UiTokens::kSettingsCancelW, UiTokens::kSettingsCancelH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), cancelButton, UiTokens::kSettingsCancelX, UiTokens::kSettingsCancelY);
    gtk_widget_add_css_class(cancelButton, "toolbar-button");
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_settingsSaved = false;
        gtk_widget_set_visible(g_settingsDialog, FALSE);
        ClearActiveModal(GTK_WINDOW(g_settingsDialog));
        RefreshStatus();
    }), nullptr);

    GtkWidget* okButton = gtk_button_new_with_label("OK");
    gtk_widget_set_size_request(okButton, UiTokens::kSettingsOkW, UiTokens::kSettingsOkH + 2);
    gtk_fixed_put(GTK_FIXED(fixed), okButton, UiTokens::kSettingsOkX, UiTokens::kSettingsOkY);
    gtk_widget_add_css_class(okButton, "toolbar-button");
    gtk_widget_add_css_class(okButton, "suggested-action");
    g_signal_connect(okButton, "clicked", G_CALLBACK(+[](GtkWidget*, gpointer) {
        if (SaveSettingsToConfig()) {
            g_settingsSaved = true;
            gtk_widget_set_visible(g_settingsDialog, FALSE);
            ClearActiveModal(GTK_WINDOW(g_settingsDialog));
            RefreshStatus();
        }
    }), nullptr);

    g_signal_connect(g_settingsDialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer) {
        g_settingsDialog = nullptr;
    }), nullptr);

    g_signal_connect(g_settingsDialog, "close-request", G_CALLBACK(+[](GtkWidget*, gpointer) -> gboolean {
        if (g_settingsDialog != nullptr) {
            gtk_widget_set_visible(g_settingsDialog, FALSE);
            ClearActiveModal(GTK_WINDOW(g_settingsDialog));
            RefreshStatus();
        }
        return TRUE;
    }), nullptr);

    gtk_window_set_titlebar(
        GTK_WINDOW(g_settingsDialog),
        CreateWin10Titlebar(GTK_WINDOW(g_settingsDialog),
                            "DLNA Server Settings",
                            WindowChrome::Dialog));

    LoadSettingsFromConfig();
    if (g_dumpGeometry) {
        PresentModalChild(GTK_WINDOW(g_settingsDialog), GTK_WINDOW(g_mainWindow));
        DumpWindowGeometry("settings", g_settingsDialog);
        return false;
    }
    if (g_dumpMsgBoxParent) {
        g_activeModal = GTK_WINDOW(g_settingsDialog);
        gtk_window_set_transient_for(GTK_WINDOW(g_settingsDialog), GTK_WINDOW(g_mainWindow));
        gtk_window_set_modal(GTK_WINDOW(g_settingsDialog), TRUE);
        gtk_window_present(GTK_WINDOW(g_settingsDialog));
        return false;
    }
    PresentModalChild(GTK_WINDOW(g_settingsDialog), GTK_WINDOW(g_mainWindow));
    while (gtk_widget_get_visible(g_settingsDialog)) {
        g_main_context_iteration(nullptr, TRUE);
    }
    if (g_settingsSaved) {
        RefreshSourceList();
        RefreshStatus();
    }
    ClearActiveModal(GTK_WINDOW(g_settingsDialog));
    return g_settingsRestartRequested;
}

void LayoutMainWindow(int width, int height) {
    gtk_widget_set_size_request(g_toolbar, width, UiTokens::kToolbarHeight);

    const int buttonTop = (UiTokens::kToolbarHeight - UiTokens::kButtonHeight) / 2;
    const int settingsLeft = width - UiTokens::kGutter - UiTokens::kSettingsButtonWidth;
    const int startLeft = settingsLeft - UiTokens::kButtonGap - UiTokens::kStartStopButtonWidth;
    const int deleteLeft = startLeft - UiTokens::kButtonGap - UiTokens::kDeleteButtonWidth;
    const int addLeft = deleteLeft - UiTokens::kButtonGap - UiTokens::kAddButtonWidth;

    const int titleRight = width - (UiTokens::kAddButtonWidth + UiTokens::kDeleteButtonWidth +
                                    UiTokens::kStartStopButtonWidth + UiTokens::kSettingsButtonWidth +
                                    UiTokens::kButtonGap * 4 + UiTokens::kGutter);
    gtk_widget_set_size_request(g_title, titleRight - UiTokens::kGutter, UiTokens::kToolbarHeight);
    GtkWidget* fixed = gtk_widget_get_parent(g_toolbar);
    gtk_fixed_move(GTK_FIXED(fixed), g_title, UiTokens::kGutter, 0);

    gtk_fixed_move(GTK_FIXED(fixed), g_addButton, addLeft, buttonTop);
    gtk_fixed_move(GTK_FIXED(fixed), g_removeButton, deleteLeft, buttonTop);
    gtk_fixed_move(GTK_FIXED(fixed), g_startStopButton, startLeft, buttonTop);
    gtk_fixed_move(GTK_FIXED(fixed), g_settingsButton, settingsLeft, buttonTop);

    gtk_fixed_move(GTK_FIXED(fixed), g_status, UiTokens::kGutter, UiTokens::kToolbarHeight);

    const int kListTop = UiTokens::kToolbarHeight + UiTokens::kStatusHeight + UiTokens::kListTopGap;
    const int ringSpace = UiTokens::kFocusRingThickness + UiTokens::kFocusRingGap;
    const int listLeft = UiTokens::kGutter + ringSpace;
    const int listTop = kListTop + ringSpace;
    const int listWidth = width - (UiTokens::kGutter * 2) - (ringSpace * 2);
    const int listHeight = height - kListTop - UiTokens::kGutter - (ringSpace * 2);
    gtk_fixed_move(GTK_FIXED(fixed), g_sourcesScrolled, listLeft, listTop);
    gtk_widget_set_size_request(g_sourcesScrolled, listWidth, listHeight);

    gtk_fixed_move(GTK_FIXED(fixed), g_emptyState, UiTokens::kGutter + 16, kListTop + 16);
}

void RefreshEmptyState() {
    bool hasSources = gtk_widget_get_first_child(g_sources) != nullptr;
    gtk_widget_set_visible(g_emptyState, !hasSources);
}

bool HasSourceSelection() {
    if (g_sources == nullptr) return false;
    GtkListBoxRow* selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(g_sources));
    return selected != nullptr;
}

bool IsShowingOverrideSources() {
    return AppConfig.HasRuntimeSourceOverride() && g_state == ServerUiState::Running;
}

void RefreshDeleteButton() {
    if (g_removeButton == nullptr) return;
    const bool transition =
        g_state == ServerUiState::Starting ||
        g_state == ServerUiState::Stopping;
    const bool scanBusy =
        g_scanInProgress.load() ||
        DLNAServer.IsInitialScanInProgress();
    const bool overrideSources =
        AppConfig.HasRuntimeSourceOverride() &&
        g_state == ServerUiState::Running;
    const bool hasSelection = HasSourceSelection();
    const bool enabled =
        hasSelection &&
        !transition &&
        !scanBusy &&
        !overrideSources;
    gtk_widget_set_sensitive(g_removeButton, enabled);
}

void OnSourceFocusChanged(GtkEventControllerFocus*, gboolean, gpointer) {
    g_sourceListHasFocus = true;
    RefreshDeleteButton();
}

void OnSourceFocusLeave(GtkEventControllerFocus*, gpointer) {
    g_sourceListHasFocus = false;
    RefreshDeleteButton();
}

void RefreshStatus() {
    gtk_label_set_text(GTK_LABEL(g_status), "");

    if (g_state == ServerUiState::Starting) {
        gtk_label_set_text(GTK_LABEL(g_status), "starting server...");
    } else if (g_state == ServerUiState::Stopping) {
        gtk_label_set_text(GTK_LABEL(g_status), "stopping server...");
    } else if (g_state == ServerUiState::Running) {
        std::string label;
        if (AppConfig.HasRuntimeSourceOverride()) {
            label = "temporary source";
        } else {
            label = (DLNAServer.IsInitialScanInProgress() || g_scanInProgress.load())
                ? " scanning..."
                : "Server running";
        }
        gtk_label_set_text(GTK_LABEL(g_status), label.c_str());
    }

    const bool transition =
        g_state == ServerUiState::Starting ||
        g_state == ServerUiState::Stopping;
    const bool scanBusy =
        g_scanInProgress.load() ||
        DLNAServer.IsInitialScanInProgress();

    gtk_widget_set_sensitive(
        g_startStopButton,
        !transition);
    gtk_button_set_label(
        GTK_BUTTON(g_startStopButton),
        g_state == ServerUiState::Running ? "Stop" : "Start");

    gtk_widget_set_sensitive(
        g_addButton,
        !transition && !scanBusy);
    gtk_button_set_label(
        GTK_BUTTON(g_addButton),
        g_state == ServerUiState::Running ? "Scan" : "Add");

    gtk_widget_set_sensitive(
        g_settingsButton,
        TRUE);

    RefreshDeleteButton();

    if (!transition && !scanBusy) {
        RefreshEmptyState();
    }
}

void RefreshSourceList() {
    GtkWidget* rowWidget = gtk_widget_get_first_child(g_sources);
    while (rowWidget != nullptr) {
        GtkWidget* next = gtk_widget_get_next_sibling(rowWidget);
        gtk_list_box_remove(GTK_LIST_BOX(g_sources), rowWidget);
        rowWidget = next;
    }
    for (const auto& source : AppConfig.mediaSources) {
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* rowLabel = gtk_label_new(ToUtf8(source.path).c_str());
        gtk_label_set_xalign(GTK_LABEL(rowLabel), 0.0f);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rowLabel);
        gtk_list_box_append(GTK_LIST_BOX(g_sources), row);
    }
    RefreshEmptyState();
}

void SaveSourcesFromList() {
    size_t savedCount = 0;
    std::vector<std::wstring> paths;
    GtkWidget* rowWidget = gtk_widget_get_first_child(g_sources);
    while (rowWidget != nullptr) {
        GtkWidget* rowChild = gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(rowWidget));
        if (GTK_IS_LABEL(rowChild)) {
            const gchar* text = gtk_label_get_text(GTK_LABEL(rowChild));
            if (text != nullptr && *text != '\0') {
                paths.push_back(ToWide(text));
            }
        }
        rowWidget = gtk_widget_get_next_sibling(rowWidget);
    }
    AppConfig.Mutate([&paths, &savedCount](Config& cfg) {
        std::vector<MediaSource> sources;
        sources.reserve(paths.size());
        for (const auto& path : paths) {
            MediaSource source;
            source.path = path;
            sources.push_back(source);
        }
        cfg.mediaSources = std::move(sources);
        savedCount = cfg.mediaSources.size();
    });
    AppConfig.Save();
    BeginRescan();
    LogPrint(L"Saved %d media source(s).", static_cast<int>(savedCount));
}

bool IsBusy() {
    return g_state == ServerUiState::Starting || g_state == ServerUiState::Stopping;
}

void SetPendingResult(ServerUiState state, bool success, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pendingState = state;
    g_pendingSuccess = success;
    g_pendingMessage = message;
    g_hasPendingResult = true;
}

void ApplySourceOverridePayload(const std::string& payload) {
    std::vector<std::wstring> parsed = ParseQuotedCommaList(Utf8ToWide(payload));
    std::vector<MediaSource> overrideSources;
    for (auto& p : parsed) {
        if (!p.empty()) overrideSources.push_back({p});
    }
    if (IsBusy()) {
        // a start stop or restart worker is already in flight drop this
        // request rather than race it matches the WM COPYDATA IsBusy guard
        // on the windows build
        return;
    }
    if (DLNAServer.IsRunning()) {
        std::vector<MediaSource> forRestart = overrideSources;
        if (g_worker.joinable()) g_worker.join();
        g_state = ServerUiState::Stopping;
        RefreshStatus();
        g_worker = std::thread([forRestart]() {
            RunGuarded(L"gtk4-source-override-restart", [forRestart]() {
                DLNAServer.Stop();
                AppConfig.SetRuntimeSourceOverride(forRestart);
                SetPendingResult(ServerUiState::Stopping, true, "");
                std::wstring reason;
                bool ok = DLNAServer.Start(reason);
                std::string message;
                if (!ok) {
                    message = "server could not start\n";
                    if (!reason.empty()) message += WideToUtf8(reason);
                }
                SetPendingResult(ok ? ServerUiState::Running : ServerUiState::Stopped, ok, message);
            });
        });
    } else {
        AppConfig.SetRuntimeSourceOverride(overrideSources);
        RefreshSourceList();
    }
}

void StartServer() {
    if (IsBusy() || g_state == ServerUiState::Running) return;
    if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled) {
        MessageBoxShow(GTK_WINDOW(g_mainWindow), "Add at least one media source.");
        return;
    }
    if (g_worker.joinable()) g_worker.join();
    g_state = ServerUiState::Starting;
    RefreshStatus();
    g_worker = std::thread([]() {
        RunGuarded(L"gtk4-start-worker", []() {
            std::wstring reason;
            bool ok = DLNAServer.Start(reason);
            std::string message;
            if (!ok) {
                message = "server could not start\n";
                if (!reason.empty()) {
                    message += WideToUtf8(reason);
                }
            }
            SetPendingResult(ok ? ServerUiState::Running : ServerUiState::Stopped, ok, message);
        });
    });
}

void StopServer() {
    if (IsBusy() || g_state != ServerUiState::Running) return;
    if (g_worker.joinable()) g_worker.join();
    g_state = ServerUiState::Stopping;
    RefreshStatus();
    g_worker = std::thread([]() {
        RunGuarded(L"gtk4-stop-worker", []() {
            DLNAServer.Stop();
            SetPendingResult(ServerUiState::Stopped, true, "");
        });
    });
}

void RestartServer() {
    if (IsBusy()) return;
    if (g_state != ServerUiState::Running) return;
    if (g_worker.joinable()) g_worker.join();
    g_state = ServerUiState::Stopping;
    RefreshStatus();
    g_worker = std::thread([]() {
        RunGuarded(L"gtk4-restart-worker", []() {
            DLNAServer.Stop();
            std::wstring reason;
            bool ok = DLNAServer.Start(reason);
            std::string message;
            if (!ok) {
                message = "server could not start\n";
                if (!reason.empty()) message += ToUtf8(reason);
            }
            SetPendingResult(ok ? ServerUiState::Running : ServerUiState::Stopped, ok, message);
        });
    });
}

void RequestClose() {
    if (g_closePending.IsPending()) return;
    if (IsBusy()) {
        if (g_state == ServerUiState::Stopping) {
            g_closePending.RequestCloseOnceStopped();
        }
        gtk_widget_set_visible(g_mainWindow, FALSE);
        return;
    }
    if (ShouldCloseNow(DLNAServer.IsRunning(), false)) {
        gtk_widget_set_visible(g_mainWindow, FALSE);
        return;
    }
    g_closePending.RequestCloseOnceStopped();
    g_state = ServerUiState::Stopping;
    RefreshStatus();
    gtk_widget_set_visible(g_mainWindow, FALSE);
    if (g_worker.joinable()) g_worker.join();
    g_worker = std::thread([]() {
        RunGuarded(L"gtk4-close-worker", []() {
            DLNAServer.Stop();
            SetPendingResult(ServerUiState::Stopped, true, "");
        });
    });
}

void ApplyPendingResult() {
    bool hasResult = false;
    ServerUiState state = ServerUiState::Stopped;
    bool success = false;
    std::string message;
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        hasResult = g_hasPendingResult;
        if (hasResult) {
            state = g_pendingState;
            success = g_pendingSuccess;
            message = g_pendingMessage;
            g_hasPendingResult = false;
        }
    }
    if (!hasResult) return;
    if (g_worker.joinable() && (state != ServerUiState::Starting || !message.empty())) {
        g_worker.join();
    }
    g_state = state;
    RefreshStatus();
    if (!success && !message.empty()) MessageBoxShow(ActiveTopLevelWindow(), message);
    if (g_closePending.ShouldCloseNowAfterOperation(state == ServerUiState::Stopped)) {
        if (g_mainWindow != nullptr) {
            gtk_window_destroy(GTK_WINDOW(g_mainWindow));
        }
        return;
    }
    if (g_closePending.ShouldStopAgainAfterOperation(state == ServerUiState::Running)) {
        StopServer();
    }
}

void BeginRescan() {
    if (g_scanInProgress.exchange(true)) return;
    RefreshStatus();
    if (g_rescanWorker.joinable()) g_rescanWorker.join();
    g_rescanWorker = std::thread([]() {
        RunGuarded(L"gtk4-rescan", []() {
            DLNAServer.Rescan();
            g_scanInProgress.store(false);
            g_idle_add(+[](gpointer) -> gboolean {
                RefreshStatus();
                return G_SOURCE_REMOVE;
            }, nullptr);
        });
    });
}

void RemoveSelectedSource() {
    if (!HasSourceSelection() || IsBusy() || g_scanInProgress.load() ||
        DLNAServer.IsInitialScanInProgress() || IsShowingOverrideSources()) {
        return;
    }
    GtkListBox* box = GTK_LIST_BOX(g_sources);
    GtkListBoxRow* selected = gtk_list_box_get_selected_row(box);
    if (selected == nullptr) return;
    gtk_list_box_remove(box, GTK_WIDGET(selected));
    SaveSourcesFromList();
    RefreshEmptyState();
    RefreshDeleteButton();
}


void OnSourceSelectionChanged(GtkListBox*, GtkListBoxRow*, gpointer) {
    RefreshDeleteButton();
}

void OnAddButtonClicked(GtkButton*, gpointer) {
    if (g_state == ServerUiState::Running) {
        BeginRescan();
    } else if (g_state == ServerUiState::Stopped) {
        PromptForMediaSource();
        RefreshEmptyState();
    }
}

void OnRemoveButtonClicked(GtkButton*, gpointer) {
    RemoveSelectedSource();
}

void OnStartStopButtonClicked(GtkButton*, gpointer) {
    if (g_state == ServerUiState::Running) {
        StopServer();
    } else if (g_state == ServerUiState::Stopped) {
        StartServer();
    }
}

void OnSettingsButtonClicked(GtkButton*, gpointer) {
    bool restartRequested = ShowSettingsDialog();
    if (restartRequested && DLNAServer.IsRunning()) {
        RestartServer();
    }
}

gboolean OnMainWindowCloseRequest(GtkWidget*, gpointer) {
    RequestClose();
    return TRUE;
}

gboolean OnPollTick(gpointer) {
    if (g_signalStop.load(std::memory_order_relaxed)) {
        RequestClose();
    }
    ApplyPendingResult();
    RefreshStatus();
    if (g_logDialog != nullptr) {
        AppendLogSinceLast();
    }
    // detect minimize to replicate Win32 SW_MINIMIZE hide behavior
    if (g_mainWindow != nullptr && gtk_widget_get_visible(GTK_WIDGET(g_mainWindow))) {
        if (GDK_IS_TOPLEVEL(g_mainWindow)) {
            GdkToplevelState state = gdk_toplevel_get_state(GDK_TOPLEVEL(g_mainWindow));
            if (state & GDK_TOPLEVEL_STATE_MINIMIZED) {
                gtk_widget_set_visible(GTK_WIDGET(g_mainWindow), FALSE);
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

// Task 15/16: shared recovery path for app-icon/second-instance and
// tray activation. Restores the hidden/minimized main window without
// stealing focus from an active modal child.
void RestoreAndFocusMainWindow() {
    if (g_mainWindow != nullptr) {
        gtk_window_present(GTK_WINDOW(g_mainWindow));
    }
    if (g_activeModal != nullptr) {
        gtk_window_present(GTK_WINDOW(g_activeModal));
    }
}

void OnSingleInstanceCommand(const std::string& cmd) {
    if (cmd == "kill") {
        g_idle_add(+[](gpointer) -> gboolean {
            RequestClose();
            return G_SOURCE_REMOVE;
        }, nullptr);
        return;
    }
    if (cmd == "show") {
        g_idle_add(+[](gpointer) -> gboolean {
            RestoreAndFocusMainWindow();
            return G_SOURCE_REMOVE;
        }, nullptr);
        return;
    }
    if (cmd.rfind("source:", 0) == 0) {
        std::string payload = cmd.substr(7);
        // marshal back onto the gtk main thread this callback runs on the
        // posix single instance listener thread and must never touch gtk
        // widgets directly see StartListening in posix single instance cpp
        auto* heapPayload = new std::string(std::move(payload));
        g_idle_add(+[](gpointer data) -> gboolean {
            std::string* p = static_cast<std::string*>(data);
            ApplySourceOverridePayload(*p);
            delete p;
            return G_SOURCE_REMOVE;
        }, heapPayload);
    }
}

gboolean OnTrayAction(gpointer) {
    RestoreAndFocusMainWindow();
    return G_SOURCE_REMOVE;
}

void OnTrayNotify(TrayNotifyAction action) {
    if (action == TrayNotifyAction::Activate) {
        g_idle_add(OnTrayAction, nullptr);
    } else if (action == TrayNotifyAction::ShowMenu) {
        // the desktop shows the exported menu model for us so nothing
        // else is needed here the menu actions map to the same handlers
        // that the in-app Logs and Help entries use
    }
}

void BuildMainWindow(GtkApplication* app) {

    GtkWidget* window = gtk_application_window_new(app);
    g_mainWindow = window;
    gtk_window_set_title(GTK_WINDOW(window), "DLNA Server");
    gtk_window_set_default_size(GTK_WINDOW(window),
                                UiTokens::kWindowWidth, UiTokens::kWindowHeight);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_widget_set_size_request(window, UiTokens::kWindowWidth, 460);

    // Task 8: prevent main window from stealing focus while a modal
    // child is active — main cannot become active / receive focus /
    // when a modal child owns the focus chain (mirrors Win32 dialog
    // modality semantics)
    GtkEventController* mainFocus = gtk_event_controller_focus_new();
    gtk_widget_add_controller(GTK_WIDGET(g_mainWindow), mainFocus);
    g_signal_connect(mainFocus, "enter", G_CALLBACK(+[](GtkEventController*, gpointer) -> gboolean {
        if (g_activeModal != nullptr) {
            gtk_window_present(GTK_WINDOW(g_activeModal));
            return FALSE; // deny focus to main window
        }
        return TRUE;
    }), nullptr);
    // win10 style title bar reuses the same header bar plus window
    // controls pattern already used by the settings log and help
    // dialogs in this same file see ShowSettingsDialog for the
    // reference implementation this mirrors.
    // The main window uses a toolbar as its title bar (matching Win32)
    // so no separate GtkHeaderBar is installed here.

    GtkWidget* fixed = gtk_fixed_new();
    gtk_widget_set_size_request(fixed, UiTokens::kWindowWidth, UiTokens::kWindowHeight);
    gtk_window_set_child(GTK_WINDOW(window), fixed);

    g_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(g_toolbar, UiTokens::kWindowWidth, UiTokens::kToolbarHeight);
    gtk_fixed_put(GTK_FIXED(fixed), g_toolbar, 0, 0);
    gtk_widget_add_css_class(g_toolbar, "toolbar");

    g_title = gtk_label_new("");
    gtk_widget_set_size_request(g_title, 200, UiTokens::kToolbarHeight);
    gtk_fixed_put(GTK_FIXED(fixed), g_title, UiTokens::kGutter, 0);
    gtk_widget_add_css_class(g_title, "window-title");
    gtk_label_set_xalign(GTK_LABEL(g_title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(g_title), PANGO_ELLIPSIZE_END);
    // toolbar title text removed per design request row still reserved for layout stability
    gtk_widget_set_visible(g_title, FALSE);

    g_addButton = gtk_button_new_with_label("Add");
    gtk_widget_set_size_request(g_addButton, UiTokens::kAddButtonWidth, -1);
    gtk_fixed_put(GTK_FIXED(fixed), g_addButton, 0, 0);
    gtk_widget_add_css_class(g_addButton, "toolbar-button");
    gtk_widget_set_tooltip_text(g_addButton, "Add media source");
    g_signal_connect(g_addButton, "clicked", G_CALLBACK(OnAddButtonClicked), nullptr);

    g_removeButton = gtk_button_new_with_label("Delete");
    gtk_widget_set_size_request(g_removeButton, UiTokens::kDeleteButtonWidth, -1);
    gtk_fixed_put(GTK_FIXED(fixed), g_removeButton, 0, 0);
    gtk_widget_add_css_class(g_removeButton, "toolbar-button");
    gtk_widget_set_tooltip_text(g_removeButton, "Delete selected source");
    g_signal_connect(g_removeButton, "clicked", G_CALLBACK(OnRemoveButtonClicked), nullptr);

    g_startStopButton = gtk_button_new_with_label("Start");
    gtk_widget_set_size_request(g_startStopButton, UiTokens::kStartStopButtonWidth, -1);
    gtk_fixed_put(GTK_FIXED(fixed), g_startStopButton, 0, 0);
    gtk_widget_add_css_class(g_startStopButton, "toolbar-button");
    gtk_widget_set_tooltip_text(g_startStopButton, "Start server");
    g_signal_connect(g_startStopButton, "clicked", G_CALLBACK(OnStartStopButtonClicked), nullptr);

    g_settingsButton = gtk_button_new_with_label("Settings");
    gtk_widget_set_size_request(g_settingsButton, UiTokens::kSettingsButtonWidth, -1);
    gtk_fixed_put(GTK_FIXED(fixed), g_settingsButton, 0, 0);
    gtk_widget_add_css_class(g_settingsButton, "toolbar-button");
    gtk_widget_set_tooltip_text(g_settingsButton, "Settings");
    g_signal_connect(g_settingsButton, "clicked", G_CALLBACK(OnSettingsButtonClicked), nullptr);

    g_status = gtk_label_new("");
    gtk_widget_set_size_request(g_status, UiTokens::kWindowWidth - UiTokens::kGutter * 2, UiTokens::kStatusHeight);
    gtk_fixed_put(GTK_FIXED(fixed), g_status, UiTokens::kGutter, UiTokens::kToolbarHeight);
    gtk_widget_add_css_class(g_status, "status-band");
    gtk_label_set_xalign(GTK_LABEL(g_status), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(g_status), PANGO_ELLIPSIZE_END);

    g_sourcesScrolled = gtk_scrolled_window_new();
    gtk_widget_add_css_class(g_sourcesScrolled, "source-list");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(g_sourcesScrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_fixed_put(GTK_FIXED(fixed), g_sourcesScrolled, 0, 0);

    g_sources = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(g_sources), GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(g_sourcesScrolled), g_sources);
    g_signal_connect(g_sources, "row-selected", G_CALLBACK(OnSourceSelectionChanged), nullptr);

    GtkEventController* sourceFocus = gtk_event_controller_focus_new();
    gtk_widget_add_controller(g_sources, sourceFocus);
    g_signal_connect(sourceFocus, "enter", G_CALLBACK(OnSourceFocusChanged), nullptr);
    g_signal_connect(sourceFocus, "leave", G_CALLBACK(OnSourceFocusLeave), nullptr);

    GtkDropTarget* sourceDropTarget = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    GType fileListGTypes[] = { GDK_TYPE_FILE_LIST };
    gtk_drop_target_set_gtypes(sourceDropTarget, fileListGTypes, 1);
    g_signal_connect(sourceDropTarget, "accept", G_CALLBACK(+[](GtkDropTarget*, GdkDrop*, gpointer) -> gboolean {
        // matches ShouldAllowSourceDrop in source drop policy h a busy or
        // running server disallows the drop exactly like the windows build
        return ShouldAllowSourceDrop(IsBusy() || DLNAServer.IsRunning()) ? TRUE : FALSE;
    }), nullptr);
    g_signal_connect(sourceDropTarget, "drop", G_CALLBACK(+[](GtkDropTarget*, const GValue* value, double, double, gpointer) -> gboolean {
        if (ShouldAllowSourceDrop(IsBusy() || DLNAServer.IsRunning()) == false) return FALSE;
        if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return FALSE;
        GSList* files = static_cast<GSList*>(g_value_get_boxed(value));
        bool addedAny = false;
        for (GSList* it = files; it != nullptr; it = it->next) {
            GFile* file = G_FILE(it->data);
            gchar* path = g_file_get_path(file);
            if (path == nullptr) continue;
            std::wstring widePath = ToWide(path);
            g_free(path);
            // directories are always accepted files are checked against
            // the same supported extension list IsSupportedLocalMediaOrPlaylistPath
            // already implements on both platforms see dlna utils cpp
            if (IsSupportedLocalMediaOrPlaylistPath(widePath)) {
                GtkWidget* row = gtk_list_box_row_new();
                GtkWidget* rowLabel = gtk_label_new(ToUtf8(widePath).c_str());
                gtk_label_set_xalign(GTK_LABEL(rowLabel), 0.0f);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), rowLabel);
                gtk_list_box_append(GTK_LIST_BOX(g_sources), row);
                addedAny = true;
            }
        }
        if (addedAny) {
            SaveSourcesFromList();
            RefreshEmptyState();
            RefreshDeleteButton();
        }
        return addedAny ? TRUE : FALSE;
    }), nullptr);
    gtk_widget_add_controller(g_sources, GTK_EVENT_CONTROLLER(sourceDropTarget));

    GtkEventController* keyController = gtk_event_controller_key_new();
    gtk_widget_add_controller(g_sources, keyController);
    g_signal_connect(keyController, "key-pressed",
                     G_CALLBACK(+[](GtkEventController*, guint keyval, guint, GdkModifierType, gpointer) -> gboolean {
        if (keyval == GDK_KEY_Delete) {
            RemoveSelectedSource();
            return TRUE;
        }
        return FALSE;
    }), nullptr);

    g_emptyState = gtk_label_new("Please add shared folders or files with Add.");
    gtk_widget_set_size_request(g_emptyState, UiTokens::kWindowWidth - UiTokens::kGutter * 2 - 16, 24);
    gtk_fixed_put(GTK_FIXED(fixed), g_emptyState, 0, 0);
    gtk_widget_add_css_class(g_emptyState, "empty-state");
    gtk_label_set_xalign(GTK_LABEL(g_emptyState), 0.0f);

    g_signal_connect(window, "close-request", G_CALLBACK(OnMainWindowCloseRequest), nullptr);

    LayoutMainWindow(UiTokens::kWindowWidth, UiTokens::kWindowHeight);
    RefreshSourceList();
    RefreshStatus();

    g_timeout_add(250, OnPollTick, nullptr);

    gtk_window_unmaximize(GTK_WINDOW(window));
    gtk_window_present(GTK_WINDOW(window));
}

// Walks every visible widget and emits one line per widget:
//   [gtk4-<tag>-geometry] class=... id=... x=... y=... w=... h=...
// coordinates are relative to the dialog toplevel so they can be diffed
// verbatim against src/ui_tokens.h (and the win32 log).
//
// gtk_widget_compute_bounds(widget, toplevel) can assert-fail or return false
// for widgets nested inside not-yet-realized container chains, silently
// dropping them. Walking the parent chain with parent-relative bounds is
// always valid (parents are always realized widgets) and yields identical
// absolute coordinates, so nothing is lost. Note: compute_bounds includes
// outline/border in the returned size; we use gtk_widget_get_width/height
// (allocation) for the reported dimensions to match Win32 pixel sizes.
static bool ComputeBoundsTo(GtkWidget* widget, GtkWidget* origin,
                             graphene_rect_t* out) {
    if (widget == origin) {
        if (!gtk_widget_compute_bounds(widget, origin, out)) return false;
        return true;
    }
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (!parent) return false;
    graphene_rect_t local;
    if (!gtk_widget_compute_bounds(widget, parent, &local)) return false;
    graphene_rect_t parentAbs;
    if (!ComputeBoundsTo(parent, origin, &parentAbs)) return false;
    out->origin.x = parentAbs.origin.x + local.origin.x;
    out->origin.y = parentAbs.origin.y + local.origin.y;
    out->size.width = gtk_widget_get_width(widget);
    out->size.height = gtk_widget_get_height(widget);
    return true;
}

void DumpWidgetTree(const char* tag, GtkWidget* widget, GtkWidget* origin) {
    if (!gtk_widget_is_visible(widget)) return;
    if (GTK_IS_WINDOW(widget)) {
        std::printf("[gtk4-%s-geometry] titlebar=%s\n",
                    tag, gtk_window_get_titlebar(GTK_WINDOW(widget)) != nullptr ? "csd" : "none");
    }
    graphene_rect_t bounds;
    if (ComputeBoundsTo(widget, origin, &bounds)) {
        const char* className = G_OBJECT_TYPE_NAME(widget);
        const char* name = gtk_widget_get_name(widget);
        std::printf("[gtk4-%s-geometry] class=%s id=%s x=%d y=%d w=%d h=%d\n",
                    tag, className ? className : "?",
                    name ? name : "0",
                    static_cast<int>(bounds.origin.x),
                    static_cast<int>(bounds.origin.y),
                    static_cast<int>(bounds.size.width),
                    static_cast<int>(bounds.size.height));
    }
    GtkWidget* child = gtk_widget_get_first_child(widget);
    while (child != nullptr) {
        DumpWidgetTree(tag, child, origin);
        child = gtk_widget_get_next_sibling(child);
    }
}

void DumpWindowGeometry(const char* tag, GtkWidget* toplevel) {
    if (!toplevel) return;
    gtk_window_present(GTK_WINDOW(toplevel));
    // gtk_window_present is async (allocates on the next frame) so drive the
    // default GMainContext under xvfb until the toplevel is allocated.
    for (int i = 0; i < 200 && gtk_widget_get_width(toplevel) <= 0; ++i)
        g_main_context_iteration(nullptr, TRUE);
    DumpWidgetTree(tag, toplevel, toplevel);
}

// builds/shows every Part-1 dialog headless, dumps its geometry, and exits
// before any modal loop so no user input is required.
void DumpAllWindowsAndExit(GtkApplication* app) {
    (void)app;
    gtk_window_present(GTK_WINDOW(g_mainWindow));
    DumpWindowGeometry("main-window", g_mainWindow);
    ShowPlaylistEntryDialog();
    PromptForMediaSource();
    ShowLogDialog();
    ShowHelpDialog(GTK_WINDOW(g_mainWindow));
    ShowSettingsDialog();
    // std::_Exit skips static destruction so the joinable single-instance
    // listener thread is not torn down during exit (that path aborts with
    // terminate called without an active exception and truncates the dump)
    std::fflush(stdout);
    std::_Exit(0);
}

// exercises the Task 6 fix: open the log dialog, hide it the way its Close
// button does (gtk_widget_hide), then open it again. before the fix the
// second ShowLogDialog() hit the stale non-null guard and returned without
// showing, so gtk_widget_get_visible() is FALSE and we exit non-zero.
// std::_Exit is used instead of std::exit so the joinable single-instance
// listener thread is not destroyed during static teardown (that path aborts
// with terminate called without an active exception); the kernel releases
// the flock when the process exits.
void DumpLogDialogReopenAndExit(GtkApplication* app) {
    (void)app;
    gtk_window_present(GTK_WINDOW(g_mainWindow));
    ShowLogDialog();
    for (int i = 0; i < 200 && !gtk_widget_get_visible(g_logDialog); ++i)
        g_main_context_iteration(nullptr, TRUE);
    gtk_widget_set_visible(g_logDialog, FALSE);
    g_main_context_iteration(nullptr, TRUE);
    ShowLogDialog();
    const bool reopened = (g_logDialog != nullptr) &&
                          gtk_widget_get_visible(GTK_WIDGET(g_logDialog));
    std::_Exit(reopened ? 0 : 2);
}

// exercises the Task 16 fix: an asynchronously-triggered failure message box
// (here forced through SetPendingResult + ApplyPendingResult) must parent to
// whichever secondary dialog is visible, falling back to the main window.
// prints [gtk4-msgbox-parent] parent=<tag> for the no-dialog case and for
// the Settings-open case, then exits. std::_Exit skips static teardown
// (the joinable single-instance listener thread aborts on std::exit).
void DumpMessageBoxParentAndExit(GtkApplication* app) {
    (void)app;
    gtk_window_present(GTK_WINDOW(g_mainWindow));
    // case 1: nothing open, the box must fall back to the main window
    SetPendingResult(ServerUiState::Stopped, false, "forced failure");
    ApplyPendingResult();
    // case 2: Settings visible, the box must parent to the settings dialog
    ShowSettingsDialog();
    for (int i = 0; i < 200 && !gtk_widget_get_visible(g_settingsDialog); ++i)
        g_main_context_iteration(nullptr, TRUE);
    SetPendingResult(ServerUiState::Stopped, false, "forced failure");
    ApplyPendingResult();
    RefreshStatus();
    MessageBoxShow(ActiveTopLevelWindow(), "parent test");
    std::fflush(stdout);
    std::_Exit(0);
}

void OnAppActivate(GtkApplication* app, gpointer) {
    BuildMainWindow(app);
    // Give the tray registration's async D-Bus round trip a moment to
    // resolve, then surface a one-time hint if no tray host is present, so
    // the user understands why there is no icon to click back to (see
    // Task 14 of dlna-server-qa-audit-and-posix-gui-lifecycle-workflow-05-08-26.md).
    // This does not block startup and does not affect window recovery,
    // which never depends on the tray (see OnSingleInstanceCommand).
    g_timeout_add_seconds(2, +[](gpointer) -> gboolean {
        if (PosixTray::IsRegistrationConfirmed() && !PosixTray::IsTrayAvailable()) {
            LogPrint(L"No system tray host detected; closing this window will "
                     L"still be reachable by re-running dlna-server-gui.");
        }
        return G_SOURCE_REMOVE;
    }, nullptr);
    if (g_dumpGeometry) {
        DumpAllWindowsAndExit(app);
        return;
    }
    if (g_dumpLogDialogReopen) {
        DumpLogDialogReopenAndExit(app);
        return;
    }
    if (g_dumpMsgBoxParent) {
        DumpMessageBoxParentAndExit(app);
        return;
    }
}

void OnAppStartup(GtkApplication* app, gpointer) {
    const std::string cssPath = ResolveBundledResourcePath("gtk/style.css");
    if (!cssPath.empty()) {
        GtkCssProvider* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_path(provider, cssPath.c_str());
        GdkDisplay* display = gdk_display_get_default();
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
    }

    // desktop dark-theme free window chrome for the dialogs
    // never depends on the system theme or AdwStyleManager
    // note: @define-color tokens from the generated style.css may not resolve
    // in a separate GtkCssProvider so these use explicit rgb() values
    const char* dialogCss =
        "window { background-color: rgb(31,31,31); color: rgb(255,255,255); }\n"
        "label { color: rgb(255,255,255); }\n"
        "frame { color: rgb(200,200,200); }\n"
         "entry { background-color: rgb(31,31,31); color: rgb(255,255,255); "
         "border: none; box-shadow: inset 0 0 0 1px rgb(88,88,88); border-radius: 8px; }\n"
        "checkbutton { color: rgb(255,255,255); }\n"
        "textview { background-color: rgb(31,31,31); color: rgb(255,255,255); }\n"
        "textview text { background-color: rgb(31,31,31); color: rgb(255,255,255); }\n"
        "scrolledwindow { background-color: rgb(31,31,31); }\n"
        "viewport { background-color: rgb(31,31,31); }\n"
        ".source-list { background-color: rgb(31,31,31); "
        "border: 1px solid rgb(88,88,88); border-radius: 0px; }\n"
        ".source-list list { padding: 2px; }\n"
        ".source-list list { background-color: rgb(31,31,31); color: rgb(255,255,255); }\n"
        ".source-list row { padding: 4px 6px; background-color: rgb(31,31,31); "
        "color: rgb(255,255,255); }\n"
        ".source-list row:selected { background-color: rgb(70,90,120); "
        "color: rgb(255,255,255); }\n"
        ".toolbar-button { background-image: none; "
        "background-color: rgb(51,51,51); "
        "border: none; "
        "box-shadow: inset 0 0 0 1px rgb(88,88,88); "
        "border-radius: 8px; color: rgb(255,255,255); "
        "min-height: 32px; }\n"
        ".toolbar-button:hover { background-image: none; "
        "background-color: rgb(62,62,62); }\n"
        ".toolbar-button:active { background-image: none; "
        "background-color: rgb(74,74,74); }\n"
        ".toolbar-button:disabled { color: rgb(132,132,132); background-image: none; "
        "background-color: rgb(51,51,51); }\n"
        ".toolbar-button:focus-visible { outline: 1px solid rgb(96,165,250); "
        "outline-offset: -4px; }\n"
        ".toolbar-button { border-bottom: 2px solid transparent; }\n"
        ".toolbar-button:hover { border-bottom: 2px solid rgb(96,165,250); }\n"
        ".source-list:focus-within { outline: 1px solid rgb(96,165,250); "
        "outline-offset: 2px; }\n"
        "window.csd, window.csd decoration { "
        "border-radius: 0px; box-shadow: none; }\n"
        "headerbar { border-radius: 0px; }\n";
    GtkCssProvider* dialogProvider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(dialogProvider, dialogCss);
#else
    gtk_css_provider_load_from_data(dialogProvider, dialogCss, -1);
#endif
    GdkDisplay* display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(dialogProvider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(dialogProvider);

    // tray icon registers with org.kde.StatusNotifierWatcher when present
    // the GUI binary is the only build that creates the tray
    GDBusConnection* connection = g_application_get_dbus_connection(G_APPLICATION(app));
    GMenu* menu = g_menu_new();
    g_menu_append(menu, "Show Window", "app.show");
    g_menu_append(menu, "Start/Stop Server", "app.startstop");
    g_menu_append(menu, "Exit", "app.quit");
    PosixTray::Initialize(connection, "dlna-server", "DLNA Server", G_MENU_MODEL(menu),
                          OnTrayNotify);
    g_object_unref(menu);
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, HandleTerminationSignal);
    std::signal(SIGTERM, HandleTerminationSignal);
    AppConfig.Load();

    bool killServer = false;
    bool explicitHeadless = false;
    std::vector<std::wstring> runtimeSources;
    bool wroteConfigOverride = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--kill-server" || arg == "-k") {
            killServer = true;
        } else if (arg == "--headless" || arg == "-h") {
            // accepted for command line symmetry with the windows and posix
            // non gui builds this binary has no headless versus windowed
            // code path distinction of its own it always creates a window
            // the flag only matters for whether a second launch is allowed
            // to auto elevate itself see ShouldStartHeadless in startup mode h
            explicitHeadless = true;
        } else if (arg == "--port" && i + 1 < argc) {
            int port = 0;
            if (TryParsePortStrict(argv[++i], port)) {
                AppConfig.Mutate([port](Config& cfg) { cfg.port = port; });
                wroteConfigOverride = true;
            }
        } else if (arg == "--name" && i + 1 < argc) {
            std::wstring name = Utf8ToWide(argv[++i]);
            AppConfig.Mutate([&name](Config& cfg) { cfg.serverName = name; });
            wroteConfigOverride = true;
        } else if (arg == "--uuid" && i + 1 < argc) {
            std::wstring uuid = Utf8ToWide(argv[++i]);
            AppConfig.Mutate([&uuid](Config& cfg) { cfg.deviceUUID = uuid; });
            wroteConfigOverride = true;
        } else if (arg == "--debug") {
            AppConfig.Mutate([](Config& cfg) { cfg.debugLog = true; });
            wroteConfigOverride = true;
        } else if (arg == "--source" && i + 1 < argc) {
            ++i;
            std::vector<std::wstring> parsed = ParseQuotedCommaList(Utf8ToWide(argv[i]));
            if (parsed.empty()) {
                runtimeSources.push_back(Utf8ToWide(argv[i]));
            } else {
                for (auto& p : parsed) runtimeSources.push_back(p);
            }
        } else if (arg == "--dump-widget-geometry" || arg == "--dump-log-dialog-reopen" ||
                   arg == "--dump-msgbox-parent") {
            // handled later by the existing hidden flag stripping loop
            continue;
        } else if (!arg.empty() && arg[0] != '-') {
            // bare positional argument dropped onto the exe or passed by a
            // context menu integration is a source path on its own see the
            // both section source flag requirement in the workflow doc
            runtimeSources.push_back(Utf8ToWide(argv[i]));
        }
    }
    // saves port name uuid debug to config so the user does not have to
    // retype them next launch source overrides are deliberately excluded
    // they stay session only per the both section requirement
    if (wroteConfigOverride) {
        AppConfig.Save();
    }

    if (killServer) {
        SingleInstance::SendKill();
        return 0;
    }

    std::wstring sourcePayload;
    if (!runtimeSources.empty()) {
        sourcePayload = BuildQuotedCommaList(runtimeSources);
        std::vector<MediaSource> overrideSources;
        for (const auto& s : runtimeSources) overrideSources.push_back({s});
        AppConfig.SetRuntimeSourceOverride(overrideSources);
    }
    (void)explicitHeadless; // reserved, this binary has no separate headless UI path

    // console echo is only ever turned on for the real foreground debug
    // session every --print-* early return above this line already exited
    // before reaching here so a test flag combined with --debug never
    // gets echo turned on see IsTestOnlyFlag in cli_flags h task 8
    bool sawTestOnlyFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (IsTestOnlyFlag(std::string(argv[i]))) { sawTestOnlyFlag = true; break; }
    }
    if (AppConfig.debugLog && !sawTestOnlyFlag) {
        SetConsoleEchoEnabled(true);
    }

    // parse the hidden --dump-widget-geometry / --dump-log-dialog-reopen
    // / --dump-msgbox-parent flags before the single-instance check so test
    // dumps are not short-circuited by an existing instance handshake
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-widget-geometry") {
            g_dumpGeometry = true;
        } else if (arg == "--dump-log-dialog-reopen") {
            g_dumpLogDialogReopen = true;
        } else if (arg == "--dump-msgbox-parent") {
            g_dumpMsgBoxParent = true;
        }
    }

    // skip the single-instance handshake for dump/test flags so headless
    // geometry dumps run regardless of whether another instance holds the lock
    if (!g_dumpGeometry && !g_dumpLogDialogReopen && !g_dumpMsgBoxParent) {
        if (!SingleInstance::TryAcquireLock()) {
            if (!sourcePayload.empty()) {
                // a running instance already exists forward the override instead
                // of showing the window matches WM COPYDATA on the windows build
                if (!SingleInstance::SendSourceOverride(WideToUtf8(sourcePayload))) {
                    std::cerr << "dlna-server-gui: another instance appears to be "
                                 "starting or is unreachable; exiting without action." << std::endl;
                }
                return 0;
            }
            if (!SingleInstance::SendShowWithRetry()) {
                LogPrint(L"Another instance appears to be starting or is unreachable; exiting without action.");
                std::cerr << "dlna-server-gui: another instance appears to be "
                             "starting or is unreachable; exiting without action." << std::endl;
            }
            return 0;
        }
    }

    GtkApplication* app = nullptr;
    int result = 1;
    for (int attempt = 0; attempt < kGuiStartupMaxAttempts; ++attempt) {
        // GTK requires a valid reverse-DNS/D-Bus application identifier.
#if GLIB_CHECK_VERSION(2, 74, 0)
        app = gtk_application_new("com.github.dlna-server-14ag", G_APPLICATION_DEFAULT_FLAGS);
#else
        app = gtk_application_new("com.github.dlna-server-14ag", G_APPLICATION_FLAGS_NONE);
#endif
        // connect before register so the startup signal emitted during
        // registration reaches OnAppStartup instead of firing into the void
        g_signal_connect(app, "startup", G_CALLBACK(OnAppStartup), nullptr);
        g_signal_connect(app, "activate", G_CALLBACK(OnAppActivate), nullptr);
        GError* registerError = nullptr;
        if (g_application_register(G_APPLICATION(app), nullptr, &registerError)) {
            g_clear_error(&registerError);
            break;
        }
        LogPrint(L"GTK application registration failed (attempt %d/%d): %hs",
                 attempt + 1, kGuiStartupMaxAttempts,
                 registerError ? registerError->message : "unknown error");
        g_clear_error(&registerError);
        g_object_unref(app);
        app = nullptr;
        g_usleep(static_cast<gulong>(kGuiStartupRetryDelayMs) * 1000);
    }
    if (!app) {
        LogPrint(L"Could not connect to a display/session bus after repeated attempts.");
        std::cerr << "dlna-server-gui: could not connect to a display/session "
                     "bus after repeated attempts; exiting." << std::endl;
        return 1;
    }

    // tray menu actions
    GActionEntry entries[] = {
        {"show", +[](GSimpleAction*, GVariant*, gpointer) {
            RestoreAndFocusMainWindow();
        }, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"startstop", +[](GSimpleAction*, GVariant*, gpointer) {
            g_idle_add(+[](gpointer) -> gboolean {
                if (g_state == ServerUiState::Running) {
                    StopServer();
                } else if (g_state == ServerUiState::Stopped) {
                    StartServer();
                }
                return G_SOURCE_REMOVE;
            }, nullptr);
        }, nullptr, nullptr, nullptr, {0, 0, 0}},
        {"quit", +[](GSimpleAction*, GVariant*, gpointer) {
            RequestClose();
        }, nullptr, nullptr, nullptr, {0, 0, 0}},
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);

    SingleInstance::StartListening(OnSingleInstanceCommand);

    // strip hidden and manual-parse flags from argv so g_application_run's
    // option parser does not reject them (they were already parsed above
    // before the single-instance handshake).
    int argcKept = 0;
    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dump-widget-geometry" ||
            arg == "--dump-log-dialog-reopen" ||
            arg == "--dump-msgbox-parent") {
            continue;
        }
        if (arg == "--source" && i + 1 < argc) {
            ++i; // skip the source payload argument too
            continue;
        }
        argv[argcKept++] = argv[i];
    }
    argc = argcKept;

    result = g_application_run(G_APPLICATION(app), argc, argv);

    PosixTray::Shutdown();
    if (g_worker.joinable()) g_worker.join();
    if (g_rescanWorker.joinable()) g_rescanWorker.join();
    DLNAServer.Stop();
    SingleInstance::ReleaseLock();
    g_object_unref(app);
    return result;
}
