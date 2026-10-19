#ifndef SERVER_UI_STATE_H
#define SERVER_UI_STATE_H

// Shared server lifecycle state used by every front end
// gui drive the status band and toolbar buttons from this single enum
enum class ServerUiState {
    Stopped,
    Starting,
    Running,
    Stopping
};

// a pending result may join the worker only when the worker has finished
// the intermediate starting result of a source override restart must not join
inline bool ShouldJoinWorkerForResult(ServerUiState state) {
    return state == ServerUiState::Running || state == ServerUiState::Stopped;
}

// text for the status band shared by every front end
// scanning wins over the server state because a scan can run while stopped
inline const wchar_t* StatusTextFor(ServerUiState state, bool scanning, bool temporarySource) {
    if (scanning) return L"scanning...";
    switch (state) {
    case ServerUiState::Starting: return L"starting server...";
    case ServerUiState::Stopping: return L"stopping server...";
    case ServerUiState::Running: return temporarySource ? L"temporary source" : L"Server running";
    case ServerUiState::Stopped: break;
    }
    return L"";
}

#endif // SERVER_UI_STATE_H
