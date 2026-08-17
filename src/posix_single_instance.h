#ifndef POSIX_SINGLE_INSTANCE_H
#define POSIX_SINGLE_INSTANCE_H

#include <string>

#ifdef DLNA_POSIX

namespace SingleInstance {

// Try to acquire the single-instance file lock (flock on the fixed
// /tmp/dlna-server-<uid> instance dir, independent of XDG_RUNTIME_DIR, so the
// whole system never runs more than one instance per user). Returns true if
// we own the lock (first instance). Returns false if another instance is
// already running.
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

// sends a source colon prefixed payload to the already running instance
// payload must already be in the quoted comma list format produced by
// BuildQuotedCommaList in dlna utils h the receiving OnSingleInstanceCommand
// callback is responsible for stripping the source colon prefix and calling
// ParseQuotedCommaList itself this mirrors WM COPYDATA on the windows build
bool SendSourceOverride(const std::string& payload);

// Sends an "effective-sources" query to the running instance and stores the
// reply (one media source path per line) in outResponse. Returns true when a
// running instance answered, false when no instance is listening. Used by
// --print-effective-media-sources so a follow-up print hook reflects the
// RUNNING instance's sources instead of an empty standalone config.
bool SendEffectiveSourcesRequest(std::string* outResponse);

// sends kill to whatever instance currently holds the lock then polls
// TryAcquireLock until it succeeds or maxAttempts is exhausted returns
// true once this process owns the lock false if the previous instance
// never released it in time mirrors the bounded retry shape already
// used by SendShowWithRetry above for the symmetric startup race
bool KillExistingAndReacquire(int maxAttempts = 25, int delayMs = 200);

// Connect to the running instance's domain socket and send "kill" command
// (the receiving instance stops its server and exits). Returns true if the
// message was delivered.
bool SendKill();

// Start a background thread listening on the domain socket for IPC commands
// (e.g. "show"). The callback is invoked from the listener thread with the
// received command text (trailing newline stripped). onQuery, when non-null,
// answers an "effective-sources" query from the listening thread with the
// string returned by the provider (one source path per line).
void StartListening(void (*onCommand)(const std::string&),
                    std::string (*onQuery)() = nullptr);

// Stop the listener, release the file lock, and clean up socket/lock files.
void ReleaseLock();

} // namespace SingleInstance

#endif // DLNA_POSIX
#endif // POSIX_SINGLE_INSTANCE_H
