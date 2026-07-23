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
