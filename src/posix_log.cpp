#include "log.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

namespace {
std::mutex g_logMutex;
std::deque<std::wstring> g_lines;
constexpr size_t kMaxLogLines = 1000;

std::wstring TimestampPrefix() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm localTime{};
    localtime_r(&ts.tv_sec, &localTime);
    wchar_t buffer[64];
    std::swprintf(buffer,
                  sizeof(buffer) / sizeof(buffer[0]),
                  L"[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ",
                  localTime.tm_year + 1900,
                  localTime.tm_mon + 1,
                  localTime.tm_mday,
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  ts.tv_nsec / 1000000);
    return buffer;
}
}

void LogPrint(const wchar_t* fmt, ...) {
    wchar_t buffer[2048];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), fmt, args);
    va_end(args);

    std::wstring line = TimestampPrefix() + buffer;

    std::lock_guard<std::mutex> lock(g_logMutex);
    g_lines.push_back(line);
    if (g_lines.size() > kMaxLogLines) {
        g_lines.pop_front();
    }
    std::fwprintf(stderr, L"%ls\n", line.c_str());
}

std::wstring GetSystemLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring result;
    result.reserve(g_lines.size() * 128);
    for (const auto& line : g_lines) {
        result += line;
        result += L"\n";
    }
    return result;
}
