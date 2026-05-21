#include "config.h"
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
#include <string>

namespace {
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 48;
constexpr int kStatusHeight = 24;
constexpr int kButtonSize = 30;

std::string ToUtf8(const std::wstring& value) {
    return WideToUtf8(value);
}

std::wstring ToWide(const char* value) {
    return Utf8ToWide(value ? value : "");
}

int ParsePort(const char* value, int fallback) {
    if (!value || !*value) return fallback;
    try {
        int port = std::stoi(value);
        return (port > 0 && port <= 65535) ? port : fallback;
    } catch (...) {
        return fallback;
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
        : Fl_Window(430, 370, "DLNA Server Settings"),
          m_serverName(120, 14, 190, 24, "Server Name:"),
          m_httpPort(120, 44, 70, 24, "HTTP Port:"),
          m_filePort(270, 44, 70, 24, "File Port:"),
          m_ipWhitelist(120, 74, 290, 24, "IP Whitelist:"),
          m_runOnStartup(16, 112, 190, 24, "Run on Windows Startup"),
          m_debugLog(16, 138, 190, 24, "Debug Log (Write to file)"),
          m_artistAlbum(16, 164, 230, 24, "Add Artist/Album folders to audio"),
          m_hideAllMedia(16, 190, 230, 24, "Do not show 'All Media' folders"),
          m_flatFolders(16, 216, 190, 24, "Flat folders style"),
          m_showFileNames(16, 242, 230, 24, "Show file names instead of titles"),
          m_sortByTitle(16, 268, 230, 24, "Sort by title instead of file name"),
          m_proxyStreams(16, 294, 190, 24, "Proxy streams"),
          m_thumbVideo(260, 112, 150, 24, "Show video thumbnails"),
          m_thumbAudio(260, 138, 150, 24, "Show audio album art"),
          m_thumbImage(260, 164, 150, 24, "Show image thumbnails"),
          m_thumbQuality(340, 194, 70, 24, "Thumbnail quality:"),
          m_restartButton(7, 340, 70, 24, "Restart"),
          m_viewLogButton(84, 340, 70, 24, "View log"),
          m_cancelButton(270, 340, 70, 24, "Cancel"),
          m_okButton(347, 340, 70, 24, "OK"),
          m_saved(false),
          m_restartRequested(false) {
        LoadFromConfig();

        m_thumbQuality.add("Low");
        m_thumbQuality.add("Medium");
        m_thumbQuality.add("High");
        m_thumbQuality.deactivate();
        m_thumbVideo.deactivate();
        m_thumbAudio.deactivate();
        m_thumbImage.deactivate();

        m_restartButton.tooltip("Restart server");
        m_viewLogButton.tooltip("View log");
        m_thumbVideo.tooltip("Thumbnail support is disabled");
        m_thumbAudio.tooltip("Thumbnail support is disabled");
        m_thumbImage.tooltip("Thumbnail support is disabled");

        m_restartButton.callback(RestartClicked, this);
        m_viewLogButton.callback(ShowLog, nullptr);
        m_cancelButton.callback(CloseWindow, this);
        m_okButton.callback(OkClicked, this);
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
        m_artistAlbum.value(AppConfig.addArtistAlbumFolders ? 1 : 0);
        m_hideAllMedia.value(AppConfig.doNotShowAllMediaFolders ? 1 : 0);
        m_flatFolders.value(AppConfig.flatFolderStyle ? 1 : 0);
        m_showFileNames.value(AppConfig.showFileNamesInsteadOfTitles ? 1 : 0);
        m_sortByTitle.value(AppConfig.sortByTitle ? 1 : 0);
        m_proxyStreams.value(AppConfig.proxyStreams ? 1 : 0);
    }

    void SaveToConfig() {
        AppConfig.serverName = ToWide(m_serverName.value());
        AppConfig.port = ParsePort(m_httpPort.value(), AppConfig.port);
        AppConfig.fileServerPort = ParsePort(m_filePort.value(), AppConfig.fileServerPort);
        AppConfig.ipWhiteList = ToWide(m_ipWhitelist.value());
        AppConfig.runOnBoot = m_runOnStartup.value() != 0;
        AppConfig.debugLog = m_debugLog.value() != 0;
        AppConfig.addArtistAlbumFolders = m_artistAlbum.value() != 0;
        AppConfig.doNotShowAllMediaFolders = m_hideAllMedia.value() != 0;
        AppConfig.flatFolderStyle = m_flatFolders.value() != 0;
        AppConfig.showFileNamesInsteadOfTitles = m_showFileNames.value() != 0;
        AppConfig.sortByTitle = m_sortByTitle.value() != 0;
        AppConfig.proxyStreams = m_proxyStreams.value() != 0;
        AppConfig.Save();
        LogPrint(L"Saved settings.");
    }

    static void OkClicked(Fl_Widget*, void* data) {
        auto* self = static_cast<SettingsDialog*>(data);
        self->SaveToConfig();
        self->m_saved = true;
        self->hide();
    }

    static void RestartClicked(Fl_Widget*, void* data) {
        auto* self = static_cast<SettingsDialog*>(data);
        self->SaveToConfig();
        self->m_saved = true;
        self->m_restartRequested = true;
        self->hide();
    }

    static void ShowLog(Fl_Widget*, void*) {
        LogDialog::ShowModal();
    }

    Fl_Input m_serverName;
    Fl_Int_Input m_httpPort;
    Fl_Int_Input m_filePort;
    Fl_Input m_ipWhitelist;
    Fl_Check_Button m_runOnStartup;
    Fl_Check_Button m_debugLog;
    Fl_Check_Button m_artistAlbum;
    Fl_Check_Button m_hideAllMedia;
    Fl_Check_Button m_flatFolders;
    Fl_Check_Button m_showFileNames;
    Fl_Check_Button m_sortByTitle;
    Fl_Check_Button m_proxyStreams;
    Fl_Check_Button m_thumbVideo;
    Fl_Check_Button m_thumbAudio;
    Fl_Check_Button m_thumbImage;
    Fl_Choice m_thumbQuality;
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
          m_addButton(kWindowWidth - 170, 10, kButtonSize, kButtonSize, "+"),
          m_removeButton(kWindowWidth - 130, 10, kButtonSize, kButtonSize, "-"),
          m_startStopButton(kWindowWidth - 90, 10, kButtonSize, kButtonSize, "@>"),
          m_settingsButton(kWindowWidth - 50, 10, kButtonSize, kButtonSize, "@settings"),
          m_status(15, kToolbarHeight, kWindowWidth - 30, kStatusHeight, "DLNA Server is stopped"),
          m_sources(0, kToolbarHeight + kStatusHeight, kWindowWidth, kWindowHeight - kToolbarHeight - kStatusHeight),
          m_emptyState(15, 80, kWindowWidth - 30, 24, "Please add shared folders or files (button \"+\")") {
        color(fl_rgb_color(30, 30, 30));
        size_range(640, 460);
        callback(CloseRequested, this);

        m_toolbar.box(FL_FLAT_BOX);
        m_toolbar.color(fl_rgb_color(45, 45, 48));

        m_title.labelfont(FL_BOLD);
        m_title.labelsize(24);
        m_title.labelcolor(fl_rgb_color(220, 220, 220));
        m_title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        m_addButton.tooltip("Add media folder");
        m_removeButton.tooltip("Remove selected media folder");
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
        end();
        resizable(m_sources);
        RefreshSourceList();
        RefreshStatus();
        Fl::add_timeout(0.5, PollLog, this);
    }

    ~MainWindow() override {
        Fl::remove_timeout(PollLog, this);
        DLNAServer.Stop();
    }

    void resize(int x, int y, int width, int height) override {
        Fl_Window::resize(x, y, width, height);
        Layout(width, height);
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
        m_removeButton.active(m_sources.size() > 0);
    }

    void RefreshStatus() {
        if (DLNAServer.IsRunning()) {
            const std::string endpoint = ToUtf8(DLNAServer.GetEndpoint());
            const std::string label = "DLNA Server is running on " + endpoint;
            m_status.copy_label(label.c_str());
            m_startStopButton.copy_label("@square");
            m_startStopButton.tooltip("Stop server");
        } else {
            m_status.copy_label("DLNA Server is stopped");
            m_startStopButton.copy_label("@>");
            m_startStopButton.tooltip("Start server");
        }
        redraw();
    }

    void StartServer() {
        SaveSourcesFromList();
        if (AppConfig.mediaSources.empty()) {
            fl_alert("Add at least one media folder.");
            return;
        }
        if (!DLNAServer.Start()) {
            fl_alert("Failed to start DLNA server. Open View log for details.");
        }
        RefreshStatus();
    }

    void StopServer() {
        DLNAServer.Stop();
        RefreshStatus();
    }

    void RestartServer() {
        const bool wasRunning = DLNAServer.IsRunning();
        StopServer();
        if (wasRunning) StartServer();
    }

    static void AddSource(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        Fl_Native_File_Chooser chooser;
        chooser.title("Choose media folder");
        chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
        if (chooser.show() == 0 && chooser.filename()) {
            const std::string selected = chooser.filename();
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
        const int selected = self->m_sources.value();
        if (selected > 0) {
            self->m_sources.remove(selected);
            self->SaveSourcesFromList();
            self->RefreshEmptyState();
        }
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
        self->RefreshStatus();
        Fl::repeat_timeout(0.5, PollLog, data);
    }

    void Layout(int width, int height) {
        m_toolbar.resize(0, 0, width, kToolbarHeight);
        m_title.resize(15, 10, width - 220, 30);
        m_addButton.resize(width - 170, 10, kButtonSize, kButtonSize);
        m_removeButton.resize(width - 130, 10, kButtonSize, kButtonSize);
        m_startStopButton.resize(width - 90, 10, kButtonSize, kButtonSize);
        m_settingsButton.resize(width - 50, 10, kButtonSize, kButtonSize);
        m_status.resize(15, kToolbarHeight, width - 30, kStatusHeight);
        m_sources.resize(0, kToolbarHeight + kStatusHeight, width, height - kToolbarHeight - kStatusHeight);
        m_emptyState.resize(15, 80, width - 30, 24);
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
};
} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    AppConfig.Load();
    MainWindow window;
    window.show(argc, argv);
    return Fl::run();
}
