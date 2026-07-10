#ifndef LOG_H
#define LOG_H

#include <string>
#include <vector>

void LogPrint(const wchar_t* fmt, ...);
std::wstring GetSystemLog();

struct LogSnapshot {
    unsigned long long latestSequence = 0;
    std::wstring text; // only lines appended after the requested sequence; empty if none
};

LogSnapshot GetSystemLogSince(unsigned long long sinceSequence);

#endif // LOG_H
