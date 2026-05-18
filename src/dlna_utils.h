#ifndef DLNA_UTILS_H
#define DLNA_UTILS_H

#include <string>

struct MediaFormatInfo {
    std::wstring mimeType;
    std::wstring upnpClass;
};

struct HttpByteRange {
    bool requested = false;
    bool satisfiable = true;
    long long start = 0;
    long long end = -1;
};

std::string TrimAscii(const std::string& value);
std::string ToLowerAscii(std::string value);
std::string FindHeaderValueCaseInsensitive(const std::string& request, const std::string& headerName);
bool TryParseIntStrict(const std::string& text, int& value);
bool TryParseNonNegativeLongLong(const std::string& text, long long& value);
HttpByteRange ParseHttpRangeHeader(const std::string& rangeHeader, long long fileSize);
bool GetMediaFormatForExtension(const std::wstring& ext, MediaFormatInfo& info);
bool IsSubtitleExtension(const std::wstring& ext);
std::string SubtitleMimeForExtension(const std::wstring& ext);
bool NaturalLessWide(const std::wstring& left, const std::wstring& right);

#endif // DLNA_UTILS_H
