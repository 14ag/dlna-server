#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_sources.h"
#include "netutils.h"
#include "server.h"

#include "settings_restart.h"
#include "server_close_policy.h"
#include "input_gate.h"
#include "cli_flags.h"
#include "settings_help.h"
#include "posix_single_instance.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace {
constexpr int kWindowWidth = 440;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 56;
constexpr int kStatusHeight = 40;
constexpr int kListTopGap = 8;
constexpr int kButtonHeight = 32;
constexpr int kAddButtonWidth = 56;
constexpr int kDeleteButtonWidth = 72;
constexpr int kStartStopButtonWidth = 72;
constexpr int kSettingsButtonWidth = 82;
constexpr int kButtonGap = 8;
constexpr int kRightGutter = 16;

enum class ServerUiState {
    Stopped,
    Starting,
    Running,
    Stopping
};

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

void ShowPlaylistEntryDialog() {
    Fl_Window dialog(560, 150, "Default playlist entry");
    Fl_Input movie(110, 18, 330, 24, "Movie path:");
    Fl_Button movieBrowse(455, 18, 85, 24, "Browse...");
    Fl_Input subtitle(110, 54, 330, 24, "Subtitle path:");
    Fl_Button subtitleBrowse(455, 54, 85, 24, "Browse...");
    Fl_Button add(465, 102, 75, 26, "Add");
    struct AddState { Fl_Window* window; Fl_Input* movie; Fl_Input* subtitle; bool done; } state{ &dialog, &movie, &subtitle, false };
    movieBrowse.callback([](Fl_Widget*, void* data) {
        auto* state = static_cast<AddState*>(data);
        Fl_Native_File_Chooser chooser;
        chooser.title("Choose movie file");
        chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
        chooser.filter("Movie files\t*.{mp4,m4v,mkv,webm,avi,mov,mpg,mpeg,ts,m2ts,wmv,flv,3gp,3g2}\nAll files\t*");
        if (chooser.show() == 0 && chooser.filename()) state->movie->value(chooser.filename());
        Fl::focus(state->movie);
    }, &state);
    subtitleBrowse.callback([](Fl_Widget*, void* data) {
        auto* state = static_cast<AddState*>(data);
        Fl_Native_File_Chooser chooser;
        chooser.title("Choose subtitle file");
        chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
        chooser.filter("Subtitle files\t*.{srt,vtt,sub,ass,ssa,smi,txt}\nAll files\t*");
        if (chooser.show() == 0 && chooser.filename()) state->subtitle->value(chooser.filename());
        Fl::focus(state->subtitle);
    }, &state);
    add.callback([](Fl_Widget*, void* data) {
        auto* state = static_cast<AddState*>(data);
        state->done = true;
        state->window->hide();
    }, &state);
    dialog.set_modal();
    dialog.end();
    dialog.show();
    Fl::focus(&dialog);
    while (dialog.shown()) Fl::wait();
    if (state.done) {
        if (!AppendDefaultPlaylistEntry(movie.value(), subtitle.value())) {
            fl_alert("Could not write default playlist.");
            return;
        }
        AppConfig.defaultPlaylistEnabled = true;
        AppConfig.Save();
    }
}

std::string PromptForMediaSourceFLTK() {
    struct State {
        Fl_Window* window = nullptr;
        Fl_Input* input = nullptr;
        Fl_Return_Button* addButton = nullptr;
        bool accepted = false;
    } state;

    Fl_Window dialog(560, 196, "Add media source");
    state.window = &dialog;

    Fl_Box label(16, 14, 528, 20, "Add a folder, playlist file, or network share URL:");
    label.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    Fl_Input input(16, 42, 528, 28);
    state.input = &input;

    Fl_Box hint(16, 78, 528, 20, "Example: ftp://user:pass@server:21/media");
    hint.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    hint.labelcolor(fl_rgb_color(150, 150, 150));

    Fl_Button folderButton(16, 114, 96, 28, "Folder...");
    Fl_Button playlistButton(120, 114, 96, 28, "Playlist...");
    Fl_Return_Button addButton(406, 150, 68, 28, "Add");
    state.addButton = &addButton;
    Fl_Button cancelButton(482, 150, 62, 28, "Cancel");

    addButton.deactivate();

    input.when(FL_WHEN_CHANGED);
    input.callback([](Fl_Widget* w, void* data) {
        auto* s = static_cast<State*>(data);
        const char* text = static_cast<Fl_Input*>(w)->value();
        const int length = static_cast<int>(std::strlen(text ? text : ""));
        if (AnyFieldHasContent({ length })) {
            s->addButton->activate();
        } else {
            s->addButton->deactivate();
        }
    }, &state);

    folderButton.callback([](Fl_Widget*, void* data) {
        auto* s = static_cast<State*>(data);
        Fl_Native_File_Chooser chooser;
        chooser.title("Choose media folder");
        chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
        if (chooser.show() == 0 && chooser.filename()) {
            s->input->value(chooser.filename());
            s->input->do_callback();
        }
        Fl::focus(s->input);
    }, &state);

    playlistButton.callback([](Fl_Widget*, void* data) {
        auto* s = static_cast<State*>(data);
        Fl_Native_File_Chooser chooser;
        chooser.title("Choose playlist file");
        chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
        chooser.filter("Playlists\t*.{m3u,m3u8,pls}\nAll files\t*");
        if (chooser.show() == 0 && chooser.filename()) {
            s->input->value(chooser.filename());
            s->input->do_callback();
        }
        Fl::focus(s->input);
    }, &state);

    addButton.callback([](Fl_Widget*, void* data) {
        auto* s = static_cast<State*>(data);
        s->accepted = true;
        s->window->hide();
    }, &state);

    cancelButton.callback([](Fl_Widget*, void* data) {
        auto* s = static_cast<State*>(data);
        s->window->hide();
    }, &state);

    dialog.end();
    dialog.set_modal();
    dialog.show();
    Fl::focus(&input);
    while (dialog.shown()) {
        Fl::wait();
    }

    if (!state.accepted) return {};
    const char* raw = input.value();
    std::string result = raw ? raw : "";
    const size_t start = result.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const size_t end = result.find_last_not_of(" \t\r\n");
    return result.substr(start, end - start + 1);
}

void CloseWindow(Fl_Widget*, void* data) {
    static_cast<Fl_Window*>(data)->hide();
}

class LogDialog : public Fl_Window {
public:
    LogDialog()
        : Fl_Window(500, 360, "DLNA Server Log"),
          m_logView(7, 7, 486, 288),
          m_refreshButton(360, 328, 62, 22, "Refresh"),
          m_closeButton(430, 328, 60, 22, "Close") {
        m_logView.buffer(&m_buffer);
        m_logView.textfont(FL_COURIER);
        m_logView.textsize(12);
        m_logView.box(FL_DOWN_BOX);
        m_refreshButton.callback(RefreshClicked, this);
        m_closeButton.callback(CloseWindow, this);
        LoadInitial();
        end();
    }

    static void ShowModal() {
        LogDialog dialog;
        dialog.set_modal();
        dialog.show();
        Fl::focus(&dialog);
        while (dialog.shown()) {
            Fl::wait();
        }
    }

private:
    void LoadInitial() {
        LogSnapshot initial = GetSystemLogSince(0);
        m_buffer.text(ToUtf8(initial.text).c_str());
        m_lastSeenSequence = initial.latestSequence;
        m_logView.insert_position(m_buffer.length());
        m_logView.show_insert_position();
    }

    void AppendNew() {
        LogSnapshot delta = GetSystemLogSince(m_lastSeenSequence);
        m_lastSeenSequence = delta.latestSequence;
        if (delta.text.empty()) return;
        m_buffer.append(ToUtf8(delta.text).c_str());
        m_logView.insert_position(m_buffer.length());
        m_logView.show_insert_position();
    }

    static void RefreshClicked(Fl_Widget*, void* data) {
        static_cast<LogDialog*>(data)->AppendNew();
    }

    Fl_Text_Buffer m_buffer;
    Fl_Text_Display m_logView;
    Fl_Button m_refreshButton;
    Fl_Button m_closeButton;
    unsigned long long m_lastSeenSequence = 0;
};

class HelpDialog : public Fl_Window {
public:
    HelpDialog()
        : Fl_Window(500, 420, "DLNA Server Help"),
          m_textView(7, 7, 486, 378),
          m_closeButton(430, 393, 60, 22, "Close") {
        m_textView.buffer(&m_buffer);
        m_textView.textfont(FL_COURIER);
        m_textView.textsize(12);
        m_textView.box(FL_DOWN_BOX);
        m_closeButton.callback(CloseWindow, this);
        BuildText();
        end();
    }

    static void ShowModal() {
        HelpDialog dialog;
        dialog.set_modal();
        dialog.show();
        Fl::focus(&dialog);
        while (dialog.shown()) {
            Fl::wait();
        }
    }

private:
    void BuildText() {
        std::string text = "Command-Line Flags\n\n";
        for (const auto& flag : GetCliFlagTable()) {
            text += ToUtf8(flag.flag) + "\t" + ToUtf8(flag.meaning) + "\n";
        }
        text += "\nSettings\n\n";
        for (const auto& setting : GetSettingsHelpTable()) {
            text += ToUtf8(setting.label) + "\t" + ToUtf8(setting.meaning) + "\n";
        }
        m_buffer.text(text.c_str());
    }

    Fl_Text_Buffer m_buffer;
    Fl_Text_Display m_textView;
    Fl_Button m_closeButton;
};

class SettingsDialog : public Fl_Window {
public:
    SettingsDialog()
        : Fl_Window(500, 420, "DLNA Server Settings"),
          m_menuBar(0, 0, 500, 24),
          m_serverName(120, 38, 190, 24, "Server Name:"),
          m_httpPort(120, 68, 190, 24, "HTTP Port:"),
          m_ipWhitelist(120, 98, 350, 24, "IP Whitelist:"),
          m_debugLog(16, 162, 190, 24, "Debug Log (Write to file)"),
          m_defaultPlaylist(260, 136, 130, 24, "Default playlist"),
          m_defaultPlaylistAdd(400, 136, 70, 24, "Add..."),
          m_artistAlbum(16, 188, 230, 24, "Add Artist/Album folders to audio"),
          m_hideAllMedia(16, 214, 230, 24, "Do not show 'All Media' folders"),
          m_flatFolders(16, 240, 190, 24, "Flat folders style"),
          m_showFileNames(16, 266, 230, 24, "Show file names instead of titles"),
          m_sortByTitle(16, 292, 230, 24, "Sort by title instead of file name"),
          m_proxyStreams(16, 318, 190, 24, "Proxy streams"),
          m_backgroundScan(16, 344, 230, 24, "Background scan (auto-rescan on changes)"),
          m_cancelButton(340, 390, 70, 24, "Cancel"),
          m_okButton(417, 390, 70, 24, "OK"),
          m_saved(false),
          m_restartRequested(false) {
        LoadFromConfig();

        m_menuBar.add("Logs", 0, ShowLog, this);
        m_menuBar.add("Help", 0, ShowHelp, this);

        m_defaultPlaylistAdd.tooltip("Add default playlist entry");

        m_defaultPlaylist.callback(DefaultPlaylistToggled, this);
        m_defaultPlaylistAdd.callback(AddDefaultPlaylistEntry, this);
        m_cancelButton.callback(CloseWindow, this);
        m_okButton.callback(OkClicked, this);
        RefreshDefaultPlaylistControls();
        end();
    }

    static bool ShowModal(bool& restartRequested) {
        SettingsDialog dialog;
        dialog.set_modal();
        dialog.show();
        while (dialog.shown()) {
            Fl::wait();
        }
        restartRequested = dialog.m_restartRequested;
        return dialog.m_saved;
    }

private:
    void LoadFromConfig() {
        m_serverName.value(ToUtf8(AppConfig.serverName).c_str());
        m_httpPort.value(std::to_string(AppConfig.port).c_str());
        m_ipWhitelist.value(ToUtf8(AppConfig.ipWhiteList).c_str());
        m_debugLog.value(AppConfig.debugLog ? 1 : 0);
        m_defaultPlaylist.value(AppConfig.defaultPlaylistEnabled ? 1 : 0);
        m_artistAlbum.value(AppConfig.addArtistAlbumFolders ? 1 : 0);
        m_hideAllMedia.value(AppConfig.doNotShowAllMediaFolders ? 1 : 0);
        m_flatFolders.value(AppConfig.flatFolderStyle ? 1 : 0);
        m_showFileNames.value(AppConfig.showFileNamesInsteadOfTitles ? 1 : 0);
        m_sortByTitle.value(AppConfig.sortByTitle ? 1 : 0);
        m_proxyStreams.value(AppConfig.proxyStreams ? 1 : 0);
        m_backgroundScan.value(AppConfig.backgroundScanEnabled ? 1 : 0);
    }

bool SaveToConfig() {
        int httpPort = 0;
        if (!TryParsePortStrict(m_httpPort.value() ? m_httpPort.value() : "", httpPort)) {
            fl_alert("HTTP port must be between 1 and 65535.");
            return false;
        }
        const std::wstring serverName = ToWide(m_serverName.value());
        const std::wstring ipWhiteList = ToWide(m_ipWhitelist.value());
        const bool debugLog = m_debugLog.value() != 0;
        const bool defaultPlaylistEnabled = m_defaultPlaylist.value() != 0;
        const bool addArtistAlbumFolders = m_artistAlbum.value() != 0;
        const bool doNotShowAllMediaFolders = m_hideAllMedia.value() != 0;
        const bool flatFolderStyle = m_flatFolders.value() != 0;
        const bool showFileNamesInsteadOfTitles = m_showFileNames.value() != 0;
        const bool sortByTitle = m_sortByTitle.value() != 0;
        const bool proxyStreams = m_proxyStreams.value() != 0;
        const bool backgroundScanEnabled = m_backgroundScan.value() != 0;

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

        m_restartRequested = false;
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
                m_restartRequested = (fl_choice("%s", "No", "Yes", nullptr, prompt.c_str()) == 1);
            }
        }
        return true;
    }

    static void OkClicked(Fl_Widget*, void* data) {
        auto* self = static_cast<SettingsDialog*>(data);
        if (!self->SaveToConfig()) return;
        self->m_saved = true;
        self->hide();
    }

    static void ShowLog(Fl_Widget*, void* data) {
        LogDialog::ShowModal();
        if (data) Fl::focus(static_cast<SettingsDialog*>(data));
    }

    static void ShowHelp(Fl_Widget*, void* data) {
        HelpDialog::ShowModal();
        if (data) Fl::focus(static_cast<SettingsDialog*>(data));
    }

    void RefreshDefaultPlaylistControls() {
        if (m_defaultPlaylist.value()) m_defaultPlaylistAdd.activate();
        else m_defaultPlaylistAdd.deactivate();
    }

    static void DefaultPlaylistToggled(Fl_Widget*, void* data) {
        static_cast<SettingsDialog*>(data)->RefreshDefaultPlaylistControls();
    }

    static void AddDefaultPlaylistEntry(Fl_Widget*, void* data) {
        auto* self = static_cast<SettingsDialog*>(data);
        ShowPlaylistEntryDialog();
        self->m_defaultPlaylist.value(1);
        self->RefreshDefaultPlaylistControls();
        Fl::focus(self);
    }

    Fl_Menu_Bar m_menuBar;
    Fl_Input m_serverName;
    Fl_Int_Input m_httpPort;
    Fl_Input m_ipWhitelist;
    Fl_Check_Button m_debugLog;
    Fl_Check_Button m_defaultPlaylist;
    Fl_Button m_defaultPlaylistAdd;
    Fl_Check_Button m_artistAlbum;
    Fl_Check_Button m_hideAllMedia;
    Fl_Check_Button m_flatFolders;
    Fl_Check_Button m_showFileNames;
    Fl_Check_Button m_sortByTitle;
    Fl_Check_Button m_proxyStreams;
    Fl_Check_Button m_backgroundScan;
    Fl_Button m_cancelButton;
    Fl_Button m_okButton;
    bool m_saved;
    bool m_restartRequested;
};

class MainWindow : public Fl_Window {
public:
    MainWindow()
        : Fl_Window(kWindowWidth, kWindowHeight, "DLNA Server"),
          m_toolbar(0, 0, kWindowWidth, kToolbarHeight),
          m_title(15, 10, 240, 30, "DLNA Server"),
          m_addButton(0, 8, kAddButtonWidth, kButtonHeight, "Add"),
          m_removeButton(0, 8, kDeleteButtonWidth, kButtonHeight, "Delete"),
          m_startStopButton(0, 8, kStartStopButtonWidth, kButtonHeight, "Start"),
          m_settingsButton(0, 8, kSettingsButtonWidth, kButtonHeight, "Settings"),
          m_status(15, kToolbarHeight, kWindowWidth - 30, kStatusHeight, "DLNA Server is stopped"),
          m_sources(0, kToolbarHeight + kStatusHeight + kListTopGap, kWindowWidth, kWindowHeight - kToolbarHeight - kStatusHeight - kListTopGap),
          m_emptyState(15, kToolbarHeight + kStatusHeight + kListTopGap + 16, kWindowWidth - 30, 24, "Please add shared folders or files with Add."),
          m_state(ServerUiState::Stopped),
          m_hasPendingResult(false),
          m_pendingSuccess(false),
          m_pendingState(ServerUiState::Stopped) {
        color(fl_rgb_color(30, 30, 30));
        size_range(440, 460);
        callback(CloseRequested, this);

        m_toolbar.box(FL_FLAT_BOX);
        m_toolbar.color(fl_rgb_color(45, 45, 48));

        m_title.labelfont(FL_BOLD);
        m_title.labelsize(24);
        m_title.labelcolor(fl_rgb_color(220, 220, 220));
        m_title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        m_addButton.tooltip("Add media source");
        m_removeButton.tooltip("Delete selected source");
        m_startStopButton.tooltip("Start server");
        m_settingsButton.tooltip("Settings");

        m_status.labelcolor(fl_rgb_color(220, 220, 220));
        m_status.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);

        m_sources.box(FL_DOWN_BOX);
        m_sources.color(fl_rgb_color(30, 30, 30));
        m_sources.textcolor(fl_rgb_color(220, 220, 220));
        m_sources.selection_color(fl_rgb_color(70, 90, 120));

        m_emptyState.labelcolor(fl_rgb_color(150, 150, 150));
        m_emptyState.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        m_addButton.callback(AddSource, this);
        m_removeButton.callback(RemoveSource, this);
        m_startStopButton.callback(ToggleServer, this);
        m_settingsButton.callback(ShowSettings, this);
        m_sources.callback(SourceSelectionChanged, this);
        end();

        // Set WM class to match StartupWMClass in .desktop files
        xclass("dlna-server");

        // Load window icon from bundled resources
        const std::string iconPath = ResolveBundledResourcePath("server_icon_48.png");
        if (!iconPath.empty()) {
            Fl_PNG_Image* png = new Fl_PNG_Image(iconPath.c_str());
            if (png->w() > 0 && png->h() > 0) {
                icon(png);
            } else {
                delete png;
            }
        }

        resizable(m_sources);
        Layout(kWindowWidth, kWindowHeight);
        RefreshSourceList();
        RefreshStatus();
        Fl::add_timeout(0.5, PollLog, this);
    }

    ~MainWindow() override {
        Fl::remove_timeout(PollLog, this);
        if (m_worker.joinable()) m_worker.join();
        DLNAServer.Stop();
    }

    void resize(int x, int y, int width, int height) override {
        Fl_Window::resize(x, y, width, height);
        Layout(width, height);
    }

    int handle(int event) override {
        if (event == FL_KEYDOWN && Fl::event_key() == FL_Delete && Fl::focus() == &m_sources) {
            RemoveSelectedSource();
            return 1;
        }
        return Fl_Window::handle(event);
    }

private:
    void RefreshSourceList() {
        m_sources.clear();
        for (const auto& source : AppConfig.mediaSources) {
            m_sources.add(ToUtf8(source.path).c_str());
        }
        RefreshEmptyState();
    }

    void SaveSourcesFromList() {
        size_t savedCount = 0;
        AppConfig.Mutate([this, &savedCount](Config& cfg) {
            cfg.mediaSources.clear();
            for (int i = 1; i <= m_sources.size(); ++i) {
                const char* text = m_sources.text(i);
                if (text && *text) {
                    cfg.mediaSources.push_back({ToWide(text)});
                }
            }
            savedCount = cfg.mediaSources.size();
        });
        AppConfig.Save();
        std::thread([]() { DLNAServer.Rescan(); }).detach();
        LogPrint(L"Saved %d media source(s).", static_cast<int>(savedCount));
    }

    void RefreshEmptyState() {
        if (m_sources.size() == 0) {
            m_emptyState.show();
        } else {
            m_emptyState.hide();
        }
        if (!IsBusy() && m_sources.value() > 0) {
            m_removeButton.activate();
        } else {
            m_removeButton.deactivate();
        }
    }

    void RefreshStatus() {
        if (m_state == ServerUiState::Starting) {
            m_status.copy_label("starting server...");
            m_startStopButton.deactivate();
        } else if (m_state == ServerUiState::Stopping) {
            m_status.copy_label("stopping server...");
            m_startStopButton.deactivate();
        } else if (m_state == ServerUiState::Running) {
            std::string label;
            if (AppConfig.HasRuntimeSourceOverride()) {
                label = "temporary source";
            } else {
                const std::string endpoint = ToUtf8(DLNAServer.GetEndpoint());
                label = DLNAServer.IsInitialScanInProgress()
                    ? ("DLNA Server is running on " + endpoint + " (scanning...)")
                    : ("DLNA Server is running on " + endpoint);
            }
            m_status.copy_label(label.c_str());
            m_startStopButton.copy_label("Stop");
            m_startStopButton.tooltip("Stop server");
            m_startStopButton.activate();
        } else {
            m_status.copy_label("DLNA Server is stopped");
            m_startStopButton.copy_label("Start");
            m_startStopButton.tooltip("Start server");
            m_startStopButton.activate();
        }
        if (IsBusy() || DLNAServer.IsInitialScanInProgress()) {
            m_addButton.deactivate();
            m_removeButton.deactivate();
            m_settingsButton.deactivate();
        } else {
            m_addButton.activate();
            m_settingsButton.activate();
            RefreshEmptyState();
        }
        redraw();
    }

    bool IsBusy() const {
        return m_state == ServerUiState::Starting || m_state == ServerUiState::Stopping;
    }

    void StartServer() {
        if (IsBusy() || m_state == ServerUiState::Running) return;
        if (AppConfig.mediaSources.empty() && !AppConfig.defaultPlaylistEnabled) {
            fl_alert("Add at least one media source.");
            return;
        }
        if (m_worker.joinable()) m_worker.join();
        m_state = ServerUiState::Starting;
        RefreshStatus();
        m_worker = std::thread([this]() {
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
    }

    void StopServer() {
        if (IsBusy() || m_state != ServerUiState::Running) return;
        if (m_worker.joinable()) m_worker.join();
        m_state = ServerUiState::Stopping;
        RefreshStatus();
        m_worker = std::thread([this]() {
            DLNAServer.Stop();
            SetPendingResult(ServerUiState::Stopped, true, "");
        });
    }

    void RequestClose() {
        if (m_closing) return;
        if (IsBusy()) return;
        if (ShouldCloseNow(DLNAServer.IsRunning(), false)) {
            hide();
            return;
        }
        m_closing = true;
        m_state = ServerUiState::Stopping;
        RefreshStatus();
        if (m_worker.joinable()) m_worker.join();
        m_worker = std::thread([this]() {
            DLNAServer.Stop();
            SetPendingResult(ServerUiState::Stopped, true, "");
        });
    }

    void RestartServer() {
        if (IsBusy()) return;
        const bool wasRunning = m_state == ServerUiState::Running;
        if (!wasRunning) return;
        if (m_worker.joinable()) m_worker.join();
        m_state = ServerUiState::Stopping;
        RefreshStatus();
        m_worker = std::thread([this]() {
            DLNAServer.Stop();
            {
                std::lock_guard<std::mutex> lock(m_pendingMutex);
                m_pendingState = ServerUiState::Starting;
                m_hasPendingResult = true;
                m_pendingSuccess = true;
                m_pendingMessage.clear();
            }
            std::wstring reason;
            bool ok = DLNAServer.Start(reason);
            std::string message;
            if (!ok) {
                message = "Server stopped. Failed to restart on the new port.";
                if (!reason.empty()) {
                    message += " ";
                    message += WideToUtf8(reason);
                }
            }
            SetPendingResult(ok ? ServerUiState::Running : ServerUiState::Stopped, ok, message);
        });
    }

    void SetPendingResult(ServerUiState state, bool success, const std::string& message) {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingState = state;
        m_pendingSuccess = success;
        m_pendingMessage = message;
        m_hasPendingResult = true;
    }

    void ApplyPendingResult() {
        bool hasResult = false;
        ServerUiState state = ServerUiState::Stopped;
        bool success = false;
        std::string message;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            hasResult = m_hasPendingResult;
            if (hasResult) {
                state = m_pendingState;
                success = m_pendingSuccess;
                message = m_pendingMessage;
                m_hasPendingResult = false;
            }
        }
        if (!hasResult) return;
        if (m_worker.joinable() && (state != ServerUiState::Starting || !message.empty())) {
            m_worker.join();
        }
        m_state = state;
        RefreshStatus();
        if (!success && !message.empty()) fl_alert("%s", message.c_str());
        if (m_closing && state == ServerUiState::Stopped) {
            hide();
        }
    }

    static void AddSource(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        std::string selected = PromptForMediaSourceFLTK();
        self->RestoreMainFocus();
        if (selected.empty()) return;
        for (int i = 1; i <= self->m_sources.size(); ++i) {
            const char* existing = self->m_sources.text(i);
            if (existing && selected == existing) return;
        }
        self->m_sources.add(selected.c_str());
        self->SaveSourcesFromList();
        self->RefreshEmptyState();
    }

    static void RemoveSource(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        self->RemoveSelectedSource();
    }

    void RemoveSelectedSource() {
        if (IsBusy()) return;
        const int selected = m_sources.value();
        if (selected > 0) {
            m_sources.remove(selected);
            const int count = m_sources.size();
            if (count > 0) m_sources.value(selected <= count ? selected : count);
            SaveSourcesFromList();
            RefreshEmptyState();
        }
    }

    static void SourceSelectionChanged(Fl_Widget*, void* data) {
        static_cast<MainWindow*>(data)->RefreshEmptyState();
    }

    static void ToggleServer(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        if (DLNAServer.IsRunning()) {
            self->StopServer();
        } else {
            self->StartServer();
        }
    }

    static void ShowSettings(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        bool restartRequested = false;
        if (SettingsDialog::ShowModal(restartRequested)) {
            self->RefreshSourceList();
            self->RefreshStatus();
            if (restartRequested) self->RestartServer();
        }
        self->RestoreMainFocus();
    }

    static void CloseRequested(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        self->RequestClose();
    }

    static void PollLog(void* data) {
        auto* self = static_cast<MainWindow*>(data);
        if (g_signalStop.load(std::memory_order_relaxed)) {
            self->RequestClose();
        }
        self->ApplyPendingResult();
        self->RefreshStatus();
        Fl::repeat_timeout(0.5, PollLog, data);
    }

    void Layout(int width, int height) {
        m_toolbar.resize(0, 0, width, kToolbarHeight);
        const int buttonTop = (kToolbarHeight - kButtonHeight) / 2;
        const int settingsLeft = width - kRightGutter - kSettingsButtonWidth;
        const int startLeft = settingsLeft - kButtonGap - kStartStopButtonWidth;
        const int deleteLeft = startLeft - kButtonGap - kDeleteButtonWidth;
        const int addLeft = deleteLeft - kButtonGap - kAddButtonWidth;
        m_title.resize(15, 10, addLeft - 30, 30);
        m_addButton.resize(addLeft, buttonTop, kAddButtonWidth, kButtonHeight);
        m_removeButton.resize(deleteLeft, buttonTop, kDeleteButtonWidth, kButtonHeight);
        m_startStopButton.resize(startLeft, buttonTop, kStartStopButtonWidth, kButtonHeight);
        m_settingsButton.resize(settingsLeft, buttonTop, kSettingsButtonWidth, kButtonHeight);
        const int listTop = kToolbarHeight + kStatusHeight + kListTopGap;
        m_status.resize(15, kToolbarHeight, width - 30, kStatusHeight);
        m_sources.resize(0, listTop, width, height - listTop);
        m_emptyState.resize(15, listTop + 16, width - 30, 24);
    }

    void RestoreMainFocus() {
        Fl::focus(&m_sources);
    }

    Fl_Box m_toolbar;
    Fl_Box m_title;
    Fl_Button m_addButton;
    Fl_Button m_removeButton;
    Fl_Button m_startStopButton;
    Fl_Button m_settingsButton;
    Fl_Box m_status;
    Fl_Hold_Browser m_sources;
    Fl_Box m_emptyState;
    ServerUiState m_state;
    std::thread m_worker;
    std::mutex m_pendingMutex;
    bool m_closing = false;
    bool m_hasPendingResult;
    bool m_pendingSuccess;
    ServerUiState m_pendingState;
    std::string m_pendingMessage;
};
} // namespace

namespace {
    // Used by single-instance reveal-on-demand callback.
    MainWindow* g_mainWindow = nullptr;

    void OnSingleInstanceCommand(const std::string& cmd) {
        if (cmd == "show" && g_mainWindow) {
            // Fl::awake schedules the callback on the main FLTK thread,
            // which is required because FLTK UI operations are not
            // thread-safe and this callback runs on the listener thread.
            Fl::awake(+[](void* data) {
                static_cast<Fl_Window*>(data)->show();
            }, g_mainWindow);
        }
    }
} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, HandleTerminationSignal);
    std::signal(SIGTERM, HandleTerminationSignal);
    Fl::lock();
    AppConfig.Load();

    // Single-instance lock: if another instance is running, tell it to
    // reveal its window and exit.
    if (!SingleInstance::TryAcquireLock()) {
        SingleInstance::SendShow();
        return 0;
    }

    MainWindow window;
    g_mainWindow = &window;

    // Start IPC listener for "show" commands from second instances.
    // The listener runs on a background thread; safety for FLTK UI calls
    // is handled via Fl::awake() inside OnSingleInstanceCommand.
    SingleInstance::StartListening(OnSingleInstanceCommand);

    window.show(argc, argv);
    const int result = Fl::run();

    SingleInstance::ReleaseLock();
    g_mainWindow = nullptr;
    return result;
}
