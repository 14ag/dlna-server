#include "dlna_utils.h"

#include "netutils.h"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <limits>
#include <random>
#include <unordered_set>
#include <vector>

namespace {
bool StartsWithAsciiNoCase(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    return ToLowerAscii(value.substr(0, prefix.size())) == ToLowerAscii(prefix);
}

bool ParseRangeNumber(const std::string& text, long long& value) {
    if (text.empty()) {
        return false;
    }
    long long result = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        int digit = ch - '0';
        if (result > ((std::numeric_limits<long long>::max)() - digit) / 10) {
            return false;
        }
        result = (result * 10) + digit;
    }
    value = result;
    return true;
}

struct ExtensionFormat {
    const wchar_t* ext;
    const wchar_t* mime;
    const wchar_t* upnpClass;
    const char* dlnaProfile;
    bool byteSeek;
};

const ExtensionFormat kFormats[] = {
    { L".mp4",  L"video/mp4",               L"object.item.videoItem",             "AVC_MP4_MP_SD_AAC_MULT5", true },
    { L".m4v",  L"video/mp4",               L"object.item.videoItem",             "AVC_MP4_MP_SD_AAC_MULT5", true },
    { L".mkv",  L"video/x-matroska",        L"object.item.videoItem",             "", true },
    { L".webm", L"video/webm",              L"object.item.videoItem",             "", true },
    { L".avi",  L"video/x-msvideo",         L"object.item.videoItem",             "", true },
    { L".divx", L"video/x-msvideo",         L"object.item.videoItem",             "", true },
    { L".mov",  L"video/quicktime",         L"object.item.videoItem",             "", true },
    { L".mpg",  L"video/mpeg",              L"object.item.videoItem",             "MPEG_PS_NTSC", true },
    { L".mpeg", L"video/mpeg",              L"object.item.videoItem",             "MPEG_PS_NTSC", true },
    { L".mpe",  L"video/mpeg",              L"object.item.videoItem",             "MPEG_PS_NTSC", true },
    { L".vob",  L"video/mpeg",              L"object.item.videoItem",             "MPEG_PS_NTSC", true },
    { L".ts",   L"video/vnd.dlna.mpeg-tts", L"object.item.videoItem",             "MPEG_TS_SD_NA", true },
    { L".m2ts", L"video/vnd.dlna.mpeg-tts", L"object.item.videoItem",             "MPEG_TS_SD_NA", true },
    { L".mts",  L"video/vnd.dlna.mpeg-tts", L"object.item.videoItem",             "MPEG_TS_SD_NA", true },
    { L".wmv",  L"video/x-ms-wmv",          L"object.item.videoItem",             "WMVMED_BASE", true },
    { L".flv",  L"video/x-flv",             L"object.item.videoItem",             "", true },
    { L".3gp",  L"video/3gpp",              L"object.item.videoItem",             "", true },
    { L".3g2",  L"video/3gpp2",             L"object.item.videoItem",             "", true },
    { L".mp3",  L"audio/mpeg",          L"object.item.audioItem.musicTrack",  "MP3", true },
    { L".flac", L"audio/flac",              L"object.item.audioItem.musicTrack",  "", true },
    { L".m4a",  L"audio/mp4",               L"object.item.audioItem.musicTrack",  "AAC_ISO_320", true },
    { L".aac",  L"audio/aac",               L"object.item.audioItem.musicTrack",  "AAC_ISO_320", true },
    { L".wav",  L"audio/wav",               L"object.item.audioItem.musicTrack",  "LPCM", true },
    { L".wma",  L"audio/x-ms-wma",          L"object.item.audioItem.musicTrack",  "WMABASE", true },
    { L".ogg",  L"audio/ogg",               L"object.item.audioItem.musicTrack",  "", true },
    { L".oga",  L"audio/ogg",               L"object.item.audioItem.musicTrack",  "", true },
    { L".opus", L"audio/opus",              L"object.item.audioItem.musicTrack",  "", true },
    { L".aiff", L"audio/aiff",              L"object.item.audioItem.musicTrack",  "", true },
    { L".aif",  L"audio/aiff",              L"object.item.audioItem.musicTrack",  "", true },
    { L".ac3",  L"audio/ac3",               L"object.item.audioItem.musicTrack",  "AC3", true },
    { L".dts",  L"audio/vnd.dts",           L"object.item.audioItem.musicTrack",  "", true },
    { L".jpg",  L"image/jpeg",              L"object.item.imageItem.photo",       "JPEG_LRG", false },
    { L".jpeg", L"image/jpeg",              L"object.item.imageItem.photo",       "JPEG_LRG", false },
    { L".png",  L"image/png",               L"object.item.imageItem.photo",       "PNG_LRG", false },
    { L".gif",  L"image/gif",               L"object.item.imageItem.photo",       "GIF_LRG", false },
    { L".bmp",  L"image/bmp",               L"object.item.imageItem.photo",       "", false },
    { L".tif",  L"image/tiff",              L"object.item.imageItem.photo",       "", false },
    { L".tiff", L"image/tiff",              L"object.item.imageItem.photo",       "", false },
    { L".webp", L"image/webp",              L"object.item.imageItem.photo",       "", false },
};

MediaFormatInfo FormatInfoFromExtensionFormat(const ExtensionFormat& format) {
    MediaFormatInfo info;
    info.mimeType = format.mime;
    info.upnpClass = format.upnpClass;
    info.dlnaProfile = format.dlnaProfile;
    info.byteSeek = format.byteSeek;
    return info;
}

std::string ProtocolTail(const MediaFormatInfo& info, bool hasKnownSize) {
    const bool seek = info.byteSeek && hasKnownSize;
    std::string tail;
    if (!info.dlnaProfile.empty()) {
        tail += "DLNA.ORG_PN=" + info.dlnaProfile + ";";
    }
    tail += "DLNA.ORG_OP=";
    tail += seek ? "01" : "00";
    tail += ";DLNA.ORG_FLAGS=" + info.dlnaFlags;
    return tail;
}

std::string NarrowAscii(const std::wstring& value) {
    return WideToUtf8(value);
}
}

std::string TrimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::wstring ToLowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::string FindHeaderValueCaseInsensitive(const std::string& request, const std::string& headerName) {
    std::string needle = ToLowerAscii(headerName) + ":";
    size_t lineStart = request.find("\r\n");

    while (lineStart != std::string::npos) {
        lineStart += 2;
        size_t lineEnd = request.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd == lineStart) {
            break;
        }

        std::string line = request.substr(lineStart, lineEnd - lineStart);
        if (ToLowerAscii(line).rfind(needle, 0) == 0) {
            return TrimAscii(line.substr(headerName.size() + 1));
        }

        lineStart = lineEnd;
    }

    return std::string();
}

bool TryParseIntStrict(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }

    bool negative = false;
    size_t index = 0;
    if (text[0] == '-') {
        negative = true;
        index = 1;
    }
    if (index == text.size()) {
        return false;
    }

    long long parsed = 0;
    for (; index < text.size(); ++index) {
        char ch = text[index];
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        parsed = (parsed * 10) + (ch - '0');
        long long limit = negative ? -(static_cast<long long>((std::numeric_limits<int>::min)())) : (std::numeric_limits<int>::max)();
        if (parsed > limit) {
            return false;
        }
    }

    value = negative ? -static_cast<int>(parsed) : static_cast<int>(parsed);
    return true;
}

bool TryParseNonNegativeLongLong(const std::string& text, long long& value) {
    return ParseRangeNumber(text, value);
}

bool IsValidPort(int port) {
    return port > 0 && port <= 65535;
}

bool TryParsePortStrict(const std::string& text, int& port) {
    int parsed = 0;
    if (!TryParseIntStrict(TrimAscii(text), parsed) || !IsValidPort(parsed)) {
        return false;
    }
    port = parsed;
    return true;
}

HttpByteRange ParseHttpRangeHeader(const std::string& rangeHeader, long long fileSize) {
    HttpByteRange result;
    result.end = fileSize > 0 ? fileSize - 1 : -1;

    std::string header = TrimAscii(rangeHeader);
    if (header.empty()) {
        return result;
    }

    result.requested = true;
    if (fileSize <= 0 || !StartsWithAsciiNoCase(header, "bytes=")) {
        result.satisfiable = false;
        return result;
    }

    std::string spec = TrimAscii(header.substr(6));
    if (spec.find(',') != std::string::npos) {
        result.satisfiable = false;
        return result;
    }

    size_t dash = spec.find('-');
    if (dash == std::string::npos) {
        result.satisfiable = false;
        return result;
    }

    std::string first = TrimAscii(spec.substr(0, dash));
    std::string last = TrimAscii(spec.substr(dash + 1));

    if (first.empty()) {
        long long suffixLength = 0;
        if (!ParseRangeNumber(last, suffixLength) || suffixLength <= 0) {
            result.satisfiable = false;
            return result;
        }
        result.start = suffixLength >= fileSize ? 0 : fileSize - suffixLength;
        result.end = fileSize - 1;
        return result;
    }

    long long start = 0;
    if (!ParseRangeNumber(first, start) || start >= fileSize) {
        result.satisfiable = false;
        return result;
    }

    long long end = fileSize - 1;
    if (!last.empty() && (!ParseRangeNumber(last, end) || end < start)) {
        result.satisfiable = false;
        return result;
    }

    result.start = start;
    result.end = (std::min)(end, fileSize - 1);
    return result;
}

bool GetMediaFormatForExtension(const std::wstring& ext, MediaFormatInfo& info) {
    std::wstring lower = ToLowerWide(ext);
    for (const auto& format : kFormats) {
        if (lower == format.ext) {
            info = FormatInfoFromExtensionFormat(format);
            return true;
        }
    }
    return false;
}

std::string BuildProtocolInfo(const MediaFormatInfo& info, bool hasKnownSize) {
    return "http-get:*:" + NarrowAscii(info.mimeType) + ":" + ProtocolTail(info, hasKnownSize);
}

std::string BuildProtocolInfoForExtension(const std::wstring& ext, const std::wstring& mimeType, bool hasKnownSize) {
    MediaFormatInfo info;
    if (!GetMediaFormatForExtension(ext, info)) {
        info.mimeType = mimeType;
        info.byteSeek = hasKnownSize;
    }
    if (info.mimeType.empty()) {
        info.mimeType = mimeType;
    }
    return BuildProtocolInfo(info, hasKnownSize);
}

std::string BuildContentFeatures(const MediaFormatInfo& info, bool hasKnownSize) {
    return ProtocolTail(info, hasKnownSize);
}

std::string BuildContentFeaturesForExtension(const std::wstring& ext, const std::wstring& mimeType, bool hasKnownSize) {
    MediaFormatInfo info;
    if (!GetMediaFormatForExtension(ext, info)) {
        info.mimeType = mimeType;
        info.byteSeek = hasKnownSize;
    }
    if (info.mimeType.empty()) {
        info.mimeType = mimeType;
    }
    return BuildContentFeatures(info, hasKnownSize);
}

std::string BuildSourceProtocolInfoList() {
    std::vector<std::string> entries;
    std::unordered_set<std::string> seen;
    for (const auto& format : kFormats) {
        MediaFormatInfo info = FormatInfoFromExtensionFormat(format);
        std::string entry = BuildProtocolInfo(info, true);
        if (seen.insert(entry).second) {
            entries.push_back(entry);
        }
    }
    std::string result;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) result += ",";
        result += entries[i];
    }
    return result;
}

std::string GetDlnaServerHeader() {
#ifdef _WIN32
    return "Windows/10.0 UPnP/1.1 dlna-server/1.4.0";
#elif defined(DLNA_PLATFORM_NAME)
    return std::string(DLNA_PLATFORM_NAME) + " UPnP/1.1 dlna-server/1.4.0";
#else
    return "POSIX UPnP/1.1 dlna-server/1.4.0";
#endif
}

std::string SubtitleMimeForExtension(const std::wstring& ext) {
    std::wstring lower = ToLowerWide(ext);
    if (lower == L".srt") return "text/srt; charset=utf-8";
    if (lower == L".vtt") return "text/vtt; charset=utf-8";
    if (lower == L".ass" || lower == L".ssa") return "text/x-ssa; charset=utf-8";
    if (lower == L".smi") return "application/smil; charset=utf-8";
    return "text/plain; charset=utf-8";
}

bool NaturalLessWide(const std::wstring& left, const std::wstring& right) {
    size_t i = 0;
    size_t j = 0;
    while (i < left.size() && j < right.size()) {
        wchar_t a = left[i];
        wchar_t b = right[j];
        if (std::iswdigit(a) && std::iswdigit(b)) {
            size_t ni = i;
            size_t nj = j;
            while (ni < left.size() && std::iswdigit(left[ni])) ++ni;
            while (nj < right.size() && std::iswdigit(right[nj])) ++nj;

            std::wstring an = left.substr(i, ni - i);
            std::wstring bn = right.substr(j, nj - j);
            an.erase(0, an.find_first_not_of(L'0'));
            bn.erase(0, bn.find_first_not_of(L'0'));
            if (an.empty()) an = L"0";
            if (bn.empty()) bn = L"0";

            if (an.size() != bn.size()) return an.size() < bn.size();
            int cmp = an.compare(bn);
            if (cmp != 0) return cmp < 0;
            i = ni;
            j = nj;
            continue;
        }

        wchar_t la = static_cast<wchar_t>(std::towlower(a));
        wchar_t lb = static_cast<wchar_t>(std::towlower(b));
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }

    return left.size() < right.size();
}

std::vector<AlbumArtCandidate> BuildAlbumArtCandidateNames(const std::wstring& stem) {
    std::vector<AlbumArtCandidate> candidates;
#if defined(_WIN32)
    candidates = {
        { L"folder.jpg", L"image/jpeg" },
        { L"cover.jpg", L"image/jpeg" },
        { L"album.jpg", L"image/jpeg" },
        { L"thumb.jpg", L"image/jpeg" },
        { L"thumb.jpeg", L"image/jpeg" },
    };
#else
    candidates = {
        { L"folder.jpg", L"image/jpeg" },
        { L"folder.JPG", L"image/jpeg" },
        { L"Folder.jpg", L"image/jpeg" },
        { L"cover.jpg", L"image/jpeg" },
        { L"cover.JPG", L"image/jpeg" },
        { L"Cover.jpg", L"image/jpeg" },
        { L"album.jpg", L"image/jpeg" },
        { L"album.JPG", L"image/jpeg" },
        { L"Album.jpg", L"image/jpeg" },
        { L"thumb.jpg", L"image/jpeg" },
        { L"thumb.JPG", L"image/jpeg" },
        { L"thumb.jpeg", L"image/jpeg" },
        { L"thumb.JPEG", L"image/jpeg" },
    };
#endif
    if (!stem.empty()) {
        candidates.push_back({ stem + L".jpg", L"image/jpeg" });
        candidates.push_back({ stem + L".jpeg", L"image/jpeg" });
        candidates.push_back({ stem + L".png", L"image/png" });
    }
    return candidates;
}

unsigned int ComputeSsdpStartupJitterMilliseconds() {
    // UPnP Device Architecture: "Devices should wait a random interval (e.g.
    // 0-100ms) before sending an initial set of advertisements in order to
    // reduce the likelihood of network storms."
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned int> distribution(0, 100);
    return distribution(generator);
}

unsigned int ComputeSsdpNextAliveIntervalMilliseconds() {
    // UPnP Device Architecture: re-issue advertisements "at a
    // randomly-distributed interval of less than one-half of the
    // advertisement expiration time" (CACHE-CONTROL max-age=1800s here, so
    // strictly under 900000ms). Jitter also avoids multiple restarted
    // instances refreshing in lockstep.
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<unsigned int> distribution(12u * 60u * 1000u, 14u * 60u * 1000u + 30u * 1000u);
    return distribution(generator);
}
