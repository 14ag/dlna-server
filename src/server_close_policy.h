#ifndef SERVER_CLOSE_POLICY_H
#define SERVER_CLOSE_POLICY_H

// pure decision extracted so it can be exercised by a print flag test
// no windows types and no fltk types so it compiles on both platforms
// true means the window or process may fully close right now
// false means the close request must instead hide the window
// isRunning should be DLNAServer IsRunning read at the moment of the close request
// isBusy should be true while a start stop or restart worker thread is in flight
// closing while busy would destroy or exit while a worker thread still
// holds a pointer or a handle into the window and still posts a
// completion message or performs a config save see the workflow document
// task 1 for the full race description
inline bool ShouldCloseNow(bool isRunning, bool isBusy) {
    return !isRunning && !isBusy;
}

#endif
