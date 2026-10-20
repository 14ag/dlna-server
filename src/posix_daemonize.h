#ifndef POSIX_DAEMONIZE_H
#define POSIX_DAEMONIZE_H

#ifdef DLNA_POSIX

// true means this process should print server is up exit zero and let a
// forked child continue in the background false means it should keep the
// foreground and hold the console like ping does until it is stopped
// extracted as a pure predicate so it has a fast deterministic test see
// startup mode h and server close policy h in this codebase for the same
// extraction pattern already used for other yes no launch decisions
inline bool ShouldDetachToBackground(bool debugLogEnabled, bool isTestOnlyFlag) {
    return !debugLogEnabled && !isTestOnlyFlag;
}

// forks once the parent prints readyMessage to its own stdout waits for
// the child to signal readiness over a pipe then exits zero the child
// calls setsid to drop its controlling terminal closes stdin stdout
// stderr and continues running the caller supplied onReady function
// after the child has fully detached returns false in the parent branch
// after it has already exited so callers never see that branch return
// true in the child branch once detach is complete
// classic double role fork plus pipe handoff pattern see
// stevens advanced programming in the unix environment chapter 13
// daemon processes and the linux daemon 7 man page sysv daemons section
bool DetachToBackgroundOrPrintReady(const char* readyMessage);

#endif // DLNA_POSIX
#endif // POSIX_DAEMONIZE_H