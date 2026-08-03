#ifndef SERVER_UI_STATE_H
#define SERVER_UI_STATE_H

// Shared server lifecycle state used by every front end
// Win32 main window, FLTK gui and GTK4 gui all drive their
// status band and toolbar buttons from this single enum
enum class ServerUiState {
    Stopped,
    Starting,
    Running,
    Stopping
};

#endif // SERVER_UI_STATE_H
