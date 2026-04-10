#ifndef LOG_H
#define LOG_H

#include <string>
#include <vector>

void LogPrint(const wchar_t* fmt, ...);
std::wstring GetSystemLog();

#endif // LOG_H
