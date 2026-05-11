#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::mutex g_logMutex;
std::vector<std::wstring> g_lines;
}

void LogPrint(const wchar_t* fmt, ...) {
    wchar_t buffer[2048];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_logMutex);
    g_lines.push_back(buffer);
    std::fwprintf(stderr, L"%ls\n", buffer);
}

std::wstring GetSystemLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring result;
    for (const auto& line : g_lines) {
        result += line;
        result += L"\n";
    }
    return result;
}
