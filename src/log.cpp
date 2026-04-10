#include "log.h"
#include "config.h"
#include <windows.h>
#include <stdio.h>
#include <deque>
#include <shlwapi.h>
#include <shlobj.h>
#include <mutex>

static std::deque<std::wstring> g_logLines;
static std::mutex g_logMutex;
const size_t MAX_LOG_LINES = 1000;

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

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logLines.push_back(line);
        if (g_logLines.size() > MAX_LOG_LINES) {
            g_logLines.pop_front();
        }
    }

    if (AppConfig.debugLog) {
        wchar_t szPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath))) {
            PathAppendW(szPath, L"WinDLNAServer");
            CreateDirectoryW(szPath, NULL);
            PathAppendW(szPath, L"debug.log");
            
            FILE* fp = NULL;
            if (_wfopen_s(&fp, szPath, L"a, ccs=UTF-8") == 0 && fp) {
                fwprintf(fp, L"%s", line.c_str());
                fclose(fp);
            }
        }
    }
}

std::wstring GetSystemLog() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring res;
    res.reserve(g_logLines.size() * 128); // approximation
    for (const auto& l : g_logLines) {
        res += l;
    }
    return res;
}
