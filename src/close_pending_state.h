#ifndef CLOSE_PENDING_STATE_H
#define CLOSE_PENDING_STATE_H

// pure state extracted so it can be exercised by a print flag test
// mirrors the pattern used by server_close_policy h source_list_focus h
// and function_key_action h see those files for the same convention
//
// remembers that a window close request arrived while a server stop
// was already in flight so the close can be honored the moment that
// stop finishes instead of leaving the app hidden in the tray with no
// way to fully exit except killing the process by hand
class ClosePendingState {
public:
    // call this once when the window close handler fires and the
    // caller has already decided not to close immediately because the
    // server is currently in its stopping transition
    void RequestCloseOnceStopped() { m_pending = true; }

    bool IsPending() const { return m_pending; }

    // call this every time a start stop or restart worker operation
    // completes finalStateIsStopped is true only when that operation
    // left the server fully stopped
    // returns true when the caller should now actually close the app
    // clears the pending flag when it returns true
    bool ShouldCloseNowAfterOperation(bool finalStateIsStopped) {
        if (!m_pending) return false;
        if (finalStateIsStopped) {
            m_pending = false;
            return true;
        }
        return false;
    }

    // returns true when the caller should immediately begin another
    // stop because a close is still pending but the operation that
    // just completed left the server running instead of stopped for
    // example the stop phase of a restart succeeded and start then
    // brought the server back up before the close could be honored
    // does not clear the pending flag the next completed stop will
    bool ShouldStopAgainAfterOperation(bool finalStateIsRunning) const {
        return m_pending && finalStateIsRunning;
    }

    void Reset() { m_pending = false; }

private:
    bool m_pending = false;
};

#endif // CLOSE_PENDING_STATE_H
