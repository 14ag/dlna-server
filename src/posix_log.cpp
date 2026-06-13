#include "log.h"
#include "config.h"
#include "netutils.h"

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
std::FILE* g_debugLogFile = nullptr;
std::string g_debugLogPath;
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

std::FILE* GetDebugLogFile() {
    std::string path = WideToUtf8(AppConfig.GetConfigPath());
    const size_t slash = path.find_last_of('/');
    path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "debug.log";

    if (g_debugLogFile && g_debugLogPath == path) {
        return g_debugLogFile;
    }
    if (g_debugLogFile) {
        std::fclose(g_debugLogFile);
        g_debugLogFile = nullptr;
    }
    g_debugLogFile = std::fopen(path.c_str(), "a");
    if (g_debugLogFile) {
        g_debugLogPath = path;
    }
    return g_debugLogFile;
}
}

void LogPrint(const wchar_t* fmt, ...) {
    wchar_t buffer[2048];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), fmt, args);
    va_end(args);

    std::wstring line = TimestampPrefix() + buffer;
    const bool writeDebugLog = AppConfig.Snapshot().debugLog;

    std::lock_guard<std::mutex> lock(g_logMutex);
    g_lines.push_back(line);
    if (g_lines.size() > kMaxLogLines) {
        g_lines.pop_front();
    }
    std::fwprintf(stderr, L"%ls\n", line.c_str());
    if (writeDebugLog) {
        std::FILE* file = GetDebugLogFile();
        if (file) {
            std::fprintf(file, "%s\n", WideToUtf8(line).c_str());
            std::fflush(file);
        }
    }
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
