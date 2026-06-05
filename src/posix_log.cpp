#include "log.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::mutex g_logMutex;
std::vector<std::wstring> g_lines;
constexpr size_t kMaxLogLines = 1000;
}

void LogPrint(const wchar_t* fmt, ...) {
    wchar_t buffer[2048];
    va_list args;
    va_start(args, fmt);
    vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_logMutex);
    g_lines.push_back(buffer);
    if (g_lines.size() > kMaxLogLines) {
        g_lines.erase(g_lines.begin(), g_lines.begin() + static_cast<std::ptrdiff_t>(g_lines.size() - kMaxLogLines));
    }
    std::fwprintf(stderr, L"%ls\n", buffer);
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
