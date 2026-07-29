#ifndef LOG_H
#define LOG_H

#include <string>
#include <vector>

void LogPrint(const wchar_t* fmt, ...);
std::wstring GetSystemLog();

// Enables/disables echoing every LogPrint() line to stdout in addition to
// the in-memory ring buffer and (if DebugLog is on) debug.log. Call this
// once, right after a console is successfully attached in headless mode.
// Never call this from GUI (non-headless) startup: writing to an unattached
// CONOUT$ handle is harmless but pointless overhead on every log line.
void SetConsoleEchoEnabled(bool enabled);

// test only exposes the truncate then reuse decision so a print flag
// test can drive it against an explicit temp path with no dependency
// on AppConfig GetConfigPath at all every normal LogPrint call still
// goes through GetDebugLogFile which derives the real path and then
// calls this same function so production behavior is unchanged
#ifdef _WIN32
FILE* OpenOrReuseDebugLogFile(const std::wstring& path);
#else
std::FILE* OpenOrReuseDebugLogFile(const std::string& path);
#endif

struct LogSnapshot {
    unsigned long long latestSequence = 0;
    std::wstring text; // only lines appended after the requested sequence; empty if none
};

LogSnapshot GetSystemLogSince(unsigned long long sinceSequence);

#endif // LOG_H
