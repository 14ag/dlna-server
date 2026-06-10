#ifndef DLNA_UTILS_H
#define DLNA_UTILS_H

#include <string>
#include <vector>

struct AlbumArtCandidate {
    std::wstring fileName;
    std::wstring mimeType;
};

struct MediaFormatInfo {
    std::wstring mimeType;
    std::wstring upnpClass;
    std::string dlnaProfile;
    bool byteSeek = true;
    std::string dlnaFlags = "01700000000000000000000000000000";
};

struct HttpByteRange {
    bool requested = false;
    bool satisfiable = true;
    long long start = 0;
    long long end = -1;
};

std::string TrimAscii(const std::string& value);
std::string ToLowerAscii(std::string value);
std::wstring ToLowerWide(std::wstring value);
std::string FindHeaderValueCaseInsensitive(const std::string& request, const std::string& headerName);
bool TryParseIntStrict(const std::string& text, int& value);
bool TryParseNonNegativeLongLong(const std::string& text, long long& value);
bool IsValidPort(int port);
bool TryParsePortStrict(const std::string& text, int& port);
HttpByteRange ParseHttpRangeHeader(const std::string& rangeHeader, long long fileSize);
bool GetMediaFormatForExtension(const std::wstring& ext, MediaFormatInfo& info);
std::string BuildProtocolInfo(const MediaFormatInfo& info, bool hasKnownSize);
std::string BuildProtocolInfoForExtension(const std::wstring& ext, const std::wstring& mimeType, bool hasKnownSize);
std::string BuildContentFeatures(const MediaFormatInfo& info, bool hasKnownSize);
std::string BuildContentFeaturesForExtension(const std::wstring& ext, const std::wstring& mimeType, bool hasKnownSize);
std::string BuildSourceProtocolInfoList();
std::string GetDlnaServerHeader();
bool IsSubtitleExtension(const std::wstring& ext);
std::string SubtitleMimeForExtension(const std::wstring& ext);
bool NaturalLessWide(const std::wstring& left, const std::wstring& right);
std::vector<AlbumArtCandidate> BuildAlbumArtCandidateNames(const std::wstring& stem);

#endif // DLNA_UTILS_H
