#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Window.H>

namespace {
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 48;
constexpr int kStatusHeight = 24;
}

int main(int argc, char** argv) {
    Fl_Window window(kWindowWidth, kWindowHeight, "DLNA Server");
    window.color(fl_rgb_color(30, 30, 30));
    window.size_range(640, 460);

    Fl_Box toolbar(0, 0, kWindowWidth, kToolbarHeight);
    toolbar.box(FL_FLAT_BOX);
    toolbar.color(fl_rgb_color(45, 45, 48));

    Fl_Box title(15, 10, 240, 30, "DLNA Server");
    title.labelfont(FL_BOLD);
    title.labelsize(24);
    title.labelcolor(fl_rgb_color(220, 220, 220));
    title.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    Fl_Button addButton(kWindowWidth - 130, 10, 30, 30, "+");
    addButton.tooltip("Add media folder");

    Fl_Button startStopButton(kWindowWidth - 90, 10, 30, 30, "@>");
    startStopButton.tooltip("Start server");

    Fl_Button settingsButton(kWindowWidth - 50, 10, 30, 30, "@settings");
    settingsButton.tooltip("Settings");

    Fl_Box status(15, kToolbarHeight, kWindowWidth - 30, kStatusHeight, "DLNA Server is stopped");
    status.labelcolor(fl_rgb_color(220, 220, 220));
    status.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    Fl_Box emptyState(15, 80, kWindowWidth - 30, 24, "Please add shared folders or files (button \"+\")");
    emptyState.labelcolor(fl_rgb_color(150, 150, 150));
    emptyState.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    window.resizable(window);
    window.end();
    window.show(argc, argv);
    return Fl::run();
}
