#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Window.H>

namespace {
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 48;
constexpr int kStatusHeight = 24;
constexpr int kButtonSize = 30;
constexpr int kButtonGap = 10;

void CloseWindow(Fl_Widget*, void* data) {
    static_cast<Fl_Window*>(data)->hide();
}

class LogDialog : public Fl_Window {
public:
    LogDialog()
        : Fl_Window(400, 300, "DLNA Server Log"),
          m_logText(7, 7, 386, 268),
          m_closeButton(343, 279, 50, 14, "Close") {
        m_logText.value("Log output will appear here.");
        m_logText.textfont(FL_COURIER);
        m_logText.textsize(12);
        m_closeButton.callback(CloseWindow, this);
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
    Fl_Multiline_Output m_logText;
    Fl_Button m_closeButton;
};

class SettingsDialog : public Fl_Window {
public:
    SettingsDialog()
        : Fl_Window(420, 370, "DLNA Server Settings"),
          m_serverName(120, 14, 180, 24, "Server Name:"),
          m_httpPort(120, 44, 70, 24, "HTTP Port:"),
          m_filePort(270, 44, 70, 24, "File Port:"),
          m_ipWhitelist(120, 74, 280, 24, "IP Whitelist:"),
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
          m_thumbQuality(330, 194, 70, 24, "Thumbnail quality:"),
          m_restartButton(7, 340, 70, 24, "Restart"),
          m_viewLogButton(84, 340, 70, 24, "View log"),
          m_cancelButton(260, 340, 70, 24, "Cancel"),
          m_okButton(337, 340, 70, 24, "OK") {
        m_serverName.value("DLNA Server");
        m_httpPort.value("8200");
        m_filePort.value("8201");
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

        m_viewLogButton.callback(ShowLog, nullptr);
        m_cancelButton.callback(CloseWindow, this);
        m_okButton.callback(CloseWindow, this);
        end();
    }

    static void ShowModal() {
        SettingsDialog dialog;
        dialog.set_modal();
        dialog.show();
        while (dialog.shown()) {
            Fl::wait();
        }
    }

private:
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
};

class MainWindow : public Fl_Window {
public:
    MainWindow()
        : Fl_Window(kWindowWidth, kWindowHeight, "DLNA Server"),
          m_toolbar(0, 0, kWindowWidth, kToolbarHeight),
          m_title(15, 10, 240, 30, "DLNA Server"),
          m_addButton(kWindowWidth - 130, 10, kButtonSize, kButtonSize, "+"),
          m_startStopButton(kWindowWidth - 90, 10, kButtonSize, kButtonSize, "@>"),
          m_settingsButton(kWindowWidth - 50, 10, kButtonSize, kButtonSize, "@settings"),
          m_status(15, kToolbarHeight, kWindowWidth - 30, kStatusHeight, "DLNA Server is stopped"),
          m_sources(0, kToolbarHeight + kStatusHeight, kWindowWidth, kWindowHeight - kToolbarHeight - kStatusHeight),
          m_emptyState(15, 80, kWindowWidth - 30, 24, "Please add shared folders or files (button \"+\")"),
          m_running(false) {
        color(fl_rgb_color(30, 30, 30));
        size_range(640, 460);

        m_toolbar.box(FL_FLAT_BOX);
        m_toolbar.color(fl_rgb_color(45, 45, 48));

        m_title.labelfont(FL_BOLD);
        m_title.labelsize(24);
        m_title.labelcolor(fl_rgb_color(220, 220, 220));
        m_title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        m_addButton.tooltip("Add media folder");
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

        m_startStopButton.callback(ToggleServer, this);
        m_settingsButton.callback(ShowSettings, nullptr);
        end();
        resizable(m_sources);
        RefreshEmptyState();
    }

    void resize(int x, int y, int width, int height) override {
        Fl_Window::resize(x, y, width, height);
        Layout(width, height);
    }

private:
    static void ToggleServer(Fl_Widget*, void* data) {
        auto* self = static_cast<MainWindow*>(data);
        self->m_running = !self->m_running;
        self->m_startStopButton.copy_label(self->m_running ? "@square" : "@>");
        self->m_startStopButton.tooltip(self->m_running ? "Stop server" : "Start server");
        self->m_status.copy_label(self->m_running ? "DLNA Server is running" : "DLNA Server is stopped");
        self->redraw();
    }

    static void ShowSettings(Fl_Widget*, void*) {
        SettingsDialog::ShowModal();
    }

    void Layout(int width, int height) {
        m_toolbar.resize(0, 0, width, kToolbarHeight);
        m_title.resize(15, 10, width - 180, 30);
        m_addButton.resize(width - 130, 10, kButtonSize, kButtonSize);
        m_startStopButton.resize(width - 90, 10, kButtonSize, kButtonSize);
        m_settingsButton.resize(width - 50, 10, kButtonSize, kButtonSize);
        m_status.resize(15, kToolbarHeight, width - 30, kStatusHeight);
        m_sources.resize(0, kToolbarHeight + kStatusHeight, width, height - kToolbarHeight - kStatusHeight);
        m_emptyState.resize(15, 80, width - 30, 24);
    }

    void RefreshEmptyState() {
        if (m_sources.size() == 0) {
            m_emptyState.show();
        } else {
            m_emptyState.hide();
        }
    }

    Fl_Box m_toolbar;
    Fl_Box m_title;
    Fl_Button m_addButton;
    Fl_Button m_startStopButton;
    Fl_Button m_settingsButton;
    Fl_Box m_status;
    Fl_Hold_Browser m_sources;
    Fl_Box m_emptyState;
    bool m_running;
};
} // namespace

int main(int argc, char** argv) {
    MainWindow window;
    window.show(argc, argv);
    return Fl::run();
}
