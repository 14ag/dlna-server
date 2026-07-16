#ifndef DLNA_UTILS_H
#define DLNA_UTILS_H

#include <string>
#include <vector>

unsigned int ComputeSsdpStartupJitterMilliseconds();
unsigned int ComputeSsdpNextAliveIntervalMilliseconds();

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
std::string BuildHlsContentFeatures();
std::string BuildHlsProtocolInfo();

// Shared by BuildHlsContentFeatures, BuildSourceProtocolInfoList, and every
// contentFeatures.dlna.org header the HTTP handlers build for an item whose
// mimeType is video/mpegurl -- do not hand-copy this literal anywhere else.
inline constexpr const char* kHlsProtocolContentFeatures =
    "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";

std::string BuildSourceProtocolInfoList();
std::string GetDlnaServerHeader();
std::string SubtitleMimeForExtension(const std::wstring& ext);
bool NaturalLessWide(const std::wstring& left, const std::wstring& right);
std::vector<AlbumArtCandidate> BuildAlbumArtCandidateNames(const std::wstring& stem);

// helpers
std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);

// new source list encoding a value is wrapped in double quotes
// a literal double quote inside a value is written as two double quotes
// values are separated by a single comma
// an unquoted bare value with no comma is still accepted for one item
std::wstring BuildQuotedCommaList(const std::vector<std::wstring>& values);
std::vector<std::wstring> ParseQuotedCommaList(const std::wstring& text);

// decodes the old pipe delimited MediaSources format from before this change
// only used once per config file during Load if the new parser finds nothing
// and the raw text still contains an unescaped pipe character
std::vector<std::wstring> DecodeLegacyPipeDelimitedSources(const std::wstring& text);

// every extension GetMediaFormatForExtension recognizes plus every
// playlist extension IsPlaylistSourcePath recognizes used by both the
// drag and drop filter and the context menu registration so the set of
// supported extensions is defined in exactly one place
std::vector<std::wstring> AllSupportedSourceExtensions();

// true for a directory or for a file whose extension this server already
// knows how to serve as media or already recognizes as a playlist
// used by the source list drag and drop target to filter dropped files
bool IsSupportedLocalMediaOrPlaylistPath(const std::wstring& path);

#endif // DLNA_UTILS_H
