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

#endif // SERVER_UI_STATE_H
