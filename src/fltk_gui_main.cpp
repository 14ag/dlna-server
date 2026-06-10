#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "media_sources.h"
#include "netutils.h"
#include "server.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>
#include <algorithm>
#include <csignal>
#include <cstdlib>
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

void CloseWindow(Fl_Widget*, void* data) {
    static_cast<Fl_Window*>(data)->hide();
}

class LogDialog : public Fl_Window {
public:
    LogDialog()
        : Fl_Window(500, 360, "DLNA Server Log"),
          m_logView(7, 7, 486, 318),
          m_closeButton(430, 333, 60, 22, "Close") {
        m_logView.buffer(&m_buffer);
        m_logView.textfont(FL_COURIER);
        m_logView.textsize(12);
        m_logView.box(FL_DOWN_BOX);
        m_closeButton.callback(CloseWindow, this);
        Refresh();
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
    void Refresh() {
        const std::string logText = ToUtf8(GetSystemLog());
        m_buffer.text(logText.c_str());
        m_logView.insert_position(m_buffer.length());
        m_logView.show_insert_position();
    }

    Fl_Text_Buffer m_buffer;
    Fl_Text_Display m_logView;
    Fl_Button m_closeButton;
};

class SettingsDialog : public Fl_Window {
public:
    SettingsDialog()
        : Fl_Window(500, 370, "DLNA Server Settings"),
          m_serverName(120, 14, 190, 24, "Server Name:"),
          m_httpPort(120, 44, 70, 24, "HTTP Port:"),
          m_filePort(270, 44, 70, 24, "File Port:"),
          m_ipWhitelist(120, 74, 350, 24, "IP Whitelist:"),
          m_runOnStartup(16, 112, 190, 24, "Run on startup"),
          m_debugLog(16, 138, 190, 24, "Debug Log (Write to file)"),
          m_defaultPlaylist(260, 112, 130, 24, "Default playlist"),
          m_defaultPlaylistAdd(400, 112, 70, 24, "Add..."),
          m_artistAlbum(16, 164, 230, 24, "Add Artist/Album folders to audio"),
          m_hideAllMedia(16, 190, 230, 24, "Do not show 'All Media' folders"),
          m_flatFolders(16, 216, 190, 24, "Flat folders style"),
          m_showFileNames(16, 242, 230, 24, "Show file names instead of titles"),
          m_sortByTitle(16, 268, 230, 24, "Sort by title instead of file name"),
          m_proxyStreams(16, 294, 190, 24, "Proxy streams"),
          m_restartButton(7, 340, 70, 24, "Restart"),
          m_viewLogButton(84, 340, 70, 24, "View log"),
          m_cancelButton(340, 340, 70, 24, "Cancel"),
          m_okButton(417, 340, 70, 24, "OK"),
          m_saved(false),
          m_restartRequested(false) {
        LoadFromConfig();

        m_restartButton.tooltip("Restart server");
        m_viewLogButton.tooltip("View log");
        m_defaultPlaylistAdd.tooltip("Add default playlist entry");

        m_restartButton.callback(RestartClicked, this);
        m_viewLogButton.callback(ShowLog, this);
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
        m_filePort.value(std::to_string(AppConfig.fileServerPort).c_str());
        m_ipWhitelist.value(ToUtf8(AppConfig.ipWhiteList).c_str());
        m_runOnStartup.value(AppConfig.runOnBoot ? 1 : 0);
        m_debugLog.value(AppConfig.debugLog ? 1 : 0);
        m_defaultPlaylist.value(AppConfig.defaultPlaylistEnabled ? 1 : 0);
        m_artistAlbum.value(AppConfig.addArtistAlbumFolders ? 1 : 0);
        m_hideAllMedia.value(AppConfig.doNotShowAllMediaFolders ? 1 : 0);
        m_flatFolders.value(AppConfig.flatFolderStyle ? 1 : 0);
        m_showFileNames.value(AppConfig.showFileNamesInsteadOfTitles ? 1 : 0);
        m_sortByTitle.value(AppConfig.sortByTitle ? 1 : 0);
        m_proxyStreams.value(AppConfig.proxyStreams ? 1 : 0);
    }

    bool SaveToConfig() {
        int httpPort = 0;
        int filePort = 0;
        if (!TryParsePortStrict(m_httpPort.value() ? m_httpPort.value() : "", httpPort) ||
            !TryParsePortStrict(m_filePort.value() ? m_filePort.value() : "", filePort)) {
            fl_alert("Ports must be between 1 and 65535.");
            return false;
        }
        AppConfig.serverName = ToWide(m_serverName.value());
        AppConfig.port = httpPort;
        AppConfig.fileServerPort = filePort;
        AppConfig.ipWhiteList = ToWide(m_ipWhitelist.value());
        AppConfig.runOnBoot = m_runOnStartup.value() != 0;
        AppConfig.debugLog = m_debugLog.value() != 0;
        AppConfig.defaultPlaylistEnabled = m_defaultPlaylist.value() != 0;
        if (AppConfig.defaultPlaylistPath.empty()) AppConfig.defaultPlaylistPath = AppConfig.GetDefaultPlaylistPath();
        AppConfig.addArtistAlbumFolders = m_artistAlbum.value() != 0;
        AppConfig.doNotShowAllMediaFolders = m_hideAllMedia.value() != 0;
        AppConfig.flatFolderStyle = m_flatFolders.value() != 0;
        AppConfig.showFileNamesInsteadOfTitles = m_showFileNames.value() != 0;
        AppConfig.sortByTitle = m_sortByTitle.value() != 0;
        AppConfig.proxyStreams = m_proxyStreams.value() != 0;
        AppConfig.Save();
        LogPrint(L"Saved settings.");
        return true;
    }

    static void OkClicked(Fl_Widget*, void* data) {
        auto* self = static_cast<SettingsDialog*>(data);
        if (!self->SaveToConfig()) return;
        self->m_saved = true;
        self->hide();
    }

    static void RestartClicked(Fl_Widget*, void* data) {
        auto* self = static_cast<SettingsDialog*>(data);
        if (!self->SaveToConfig()) return;
        self->m_saved = true;
        self->m_restartRequested = true;
        self->hide();
    }

    static void ShowLog(Fl_Widget*, void* data) {
        LogDialog::ShowModal();
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

    Fl_Input m_serverName;
    Fl_Int_Input m_httpPort;
    Fl_Int_Input m_filePort;
    Fl_Input m_ipWhitelist;
    Fl_Check_Button m_runOnStartup;
    Fl_Check_Button m_debugLog;
    Fl_Check_Button m_defaultPlaylist;
    Fl_Button m_defaultPlaylistAdd;
    Fl_Check_Button m_artistAlbum;
    Fl_Check_Button m_hideAllMedia;
    Fl_Check_Button m_flatFolders;
    Fl_Check_Button m_showFileNames;
    Fl_Check_Button m_sortByTitle;
    Fl_Check_Button m_proxyStreams;
    Fl_Button m_restartButton;
    Fl_Button m_viewLogButton;
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
        AppConfig.mediaSources.clear();
        for (int i = 1; i <= m_sources.size(); ++i) {
            const char* text = m_sources.text(i);
            if (text && *text) {
                AppConfig.mediaSources.push_back({ToWide(text), true});
            }
        }
        AppConfig.Save();
        AppMedia.Scan();
        LogPrint(L"Saved %d media source(s).", static_cast<int>(AppConfig.mediaSources.size()));
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
            const std::string endpoint = ToUtf8(DLNAServer.GetEndpoint());
            const std::string label = "DLNA Server is running on " + endpoint;
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
        if (IsBusy()) {
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
            bool ok = DLNAServer.Start();
            SetPendingResult(ok ? ServerUiState::Running : ServerUiState::Stopped, ok, ok ? "" : "Failed to start DLNA server. Open View log for details.");
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
            bool ok = DLNAServer.Start();
            SetPendingResult(ok ? ServerUiState::Running : ServerUiState::Stopped, ok, ok ? "" : "Server stopped. Failed to restart on the new port.");
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
    }

    static void AddSource(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        int choice = fl_choice("Add media source", "Folder", "Playlist", "Network URL");
        std::string selected;

        if (choice == 0 || choice == 1) {
            Fl_Native_File_Chooser chooser;
            chooser.title(choice == 0 ? "Choose media folder" : "Choose playlist file");
            chooser.type(choice == 0 ? Fl_Native_File_Chooser::BROWSE_DIRECTORY : Fl_Native_File_Chooser::BROWSE_FILE);
            if (choice == 1) chooser.filter("Playlists\t*.{m3u,m3u8,pls}\nAll files\t*");
            if (chooser.show() == 0 && chooser.filename()) {
                selected = chooser.filename();
            }
            self->RestoreMainFocus();
        } else if (choice == 2) {
            const char* typed = fl_input("Network share URL", "smb://user:pass@server/share");
            if (typed) selected = typed;
            self->RestoreMainFocus();
        }

        if (!selected.empty()) {
            for (int i = 1; i <= self->m_sources.size(); ++i) {
                const char* existing = self->m_sources.text(i);
                if (existing && selected == existing) return;
            }
            self->m_sources.add(selected.c_str());
            self->SaveSourcesFromList();
            self->RefreshEmptyState();
        }
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
        if (DLNAServer.IsRunning()) {
            self->hide();
        } else {
            self->hide();
        }
    }

    static void PollLog(void* data) {
        auto* self = static_cast<MainWindow*>(data);
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
    bool m_hasPendingResult;
    bool m_pendingSuccess;
    ServerUiState m_pendingState;
    std::string m_pendingMessage;
};
} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    AppConfig.Load();
    MainWindow window;
    window.show(argc, argv);
    return Fl::run();
}
