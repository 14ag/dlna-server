#ifndef POSIX_SINGLE_INSTANCE_H
#define POSIX_SINGLE_INSTANCE_H

#include <string>

#ifdef DLNA_POSIX

namespace SingleInstance {

// Try to acquire the single-instance file lock (flock on XDG_RUNTIME_DIR or
// fallback /tmp). Returns true if we own the lock (first instance). Returns
// false if another instance is already running.
bool TryAcquireLock();

// Connect to the running instance's domain socket and send "show" command.
// Returns true if the message was delivered.
bool SendShow();

// Retries SendShow() with a short delay between attempts. Covers the
// startup race where TryAcquireLock() succeeded for the FIRST instance
// (flock is held) but that instance has not yet reached
// StartListening()'s bind()/listen() call, so an immediate connect()
// fails with ECONNREFUSED even though the peer is seconds away from
// being ready. Returns true the first time SendShow() succeeds, false
// if every attempt failed. See Task 5 of
// dlna-server-qa-audit-and-posix-gui-lifecycle-workflow-05-08-26.md.
bool SendShowWithRetry(int maxAttempts = 5, int delayMs = 200);

// Connect to the running instance's domain socket and send "kill" command
// (the receiving instance stops its server and exits). Returns true if the
// message was delivered.
bool SendKill();

// Start a background thread listening on the domain socket for IPC commands
// (e.g. "show"). The callback is invoked from the listener thread with the
// received command text (trailing newline stripped). Must be thread-safe;
// for FLTK, use Fl::awake() to marshal to the main thread.
void StartListening(void (*onCommand)(const std::string&));

// Stop the listener, release the file lock, and clean up socket/lock files.
void ReleaseLock();

} // namespace SingleInstance

#endif // DLNA_POSIX
#endif // POSIX_SINGLE_INSTANCE_H
