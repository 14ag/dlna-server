#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Window.H>

namespace {
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 48;
constexpr int kStatusHeight = 24;
constexpr int kButtonSize = 30;
constexpr int kButtonGap = 10;

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
