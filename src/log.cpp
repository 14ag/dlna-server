#include "log.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>
#include <deque>
#include <shlwapi.h>
#include <share.h>
#include <mutex>
#include <atomic>

static std::deque<std::pair<unsigned long long, std::wstring>> g_logLines;
static unsigned long long g_nextSeq = 1;
static std::mutex g_logMutex;
static FILE* g_debugLogFile = NULL;
static std::wstring g_debugLogPath;
const size_t MAX_LOG_LINES = 1000;
static std::atomic<bool> g_consoleEchoEnabled(false);

void SetConsoleEchoEnabled(bool enabled) {
    g_consoleEchoEnabled.store(enabled, std::memory_order_relaxed);
}

FILE* GetDebugLogFile() {
    std::wstring configPath = AppConfig.GetConfigPath();
    wchar_t szPath[MAX_PATH] = {};
    if (configPath.empty() || configPath.size() >= MAX_PATH) {
        return NULL;
    }
    wcscpy_s(szPath, configPath.c_str());
    PathRemoveFileSpecW(szPath);
    PathAppendW(szPath, L"debug.log");

    std::wstring path(szPath);
    if (g_debugLogFile && g_debugLogPath == path) {
        return g_debugLogFile;
    }
    if (g_debugLogFile) {
        fclose(g_debugLogFile);
        g_debugLogFile = NULL;
    }
    g_debugLogFile = _wfsopen(path.c_str(), L"a,ccs=UTF-8", _SH_DENYNO);
    if (g_debugLogFile) {
        g_debugLogPath = path;
    }
    return g_debugLogFile;
}

void LogPrint(const wchar_t* fmt, ...) {
    wchar_t msg[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, 2048, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timeBuf[64];
    swprintf_s(timeBuf, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::wstring line = std::wstring(timeBuf) + msg + L"\r\n";
    const bool writeDebugLog = AppConfig.IsDebugLogEnabled();

    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logLines.emplace_back(g_nextSeq++, line);
    if (g_logLines.size() > MAX_LOG_LINES) {
        g_logLines.pop_front();
    }
    if (writeDebugLog) {
        FILE* fp = GetDebugLogFile();
        if (fp) {
            fwprintf(fp, L"%s", line.c_str());
            fflush(fp);
        }
    }
    if (g_consoleEchoEnabled.load(std::memory_order_relaxed)) {
        fwprintf(stdout, L"%ls", line.c_str());
        fflush(stdout);
    }
}

std::wstring GetSystemLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring res;
    res.reserve(g_logLines.size() * 128);
    for (const auto& entry : g_logLines) {
        res += entry.second;
    }
    return res;
}

LogSnapshot GetSystemLogSince(unsigned long long sinceSequence) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    LogSnapshot snapshot;
    snapshot.latestSequence = g_nextSeq - 1;
    if (g_logLines.empty()) return snapshot;
    const unsigned long long oldestKept = g_logLines.front().first;
    if (oldestKept > 1 && sinceSequence < oldestKept - 1) {
        const unsigned long long dropped = oldestKept - 1 - sinceSequence;
        snapshot.text += L"[" + std::to_wstring(dropped) +
                          L" earlier log line(s) were dropped from the in-memory buffer]\r\n";
    }
    for (const auto& entry : g_logLines) {
        if (entry.first > sinceSequence) snapshot.text += entry.second;
    }
    return snapshot;
}
