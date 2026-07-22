#include "network_sources.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include "scan_cancellation.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <vector>

std::string UrlWithoutQueryOrFragment(const std::string& value);
std::string UrlEncodePathSegment(const std::string& value);
std::wstring ResolveRelativeUrl(const std::wstring& baseUrl, const std::wstring& relativeUrl);
std::string RewriteHlsManifestUrisToAbsolute(const std::wstring& manifestUrl, const std::string& manifestText);

namespace {
    class ScopeGuard {
    public:
        template<typename F>
        explicit ScopeGuard(F f) : m_f(std::move(f)) {}
        ~ScopeGuard() { if (m_f) m_f(); }
        ScopeGuard(ScopeGuard&& other) noexcept : m_f(std::move(other.m_f)) {}
        ScopeGuard& operator=(ScopeGuard&& other) noexcept { if (this != &other) { m_f = std::move(other.m_f); } return *this; }
        void Dismiss() { m_f = nullptr; }
    private:
        std::function<void()> m_f;
    };
}

#include <curl/curl.h>

namespace {
constexpr int kMaxCurlOutputBytes = 4 * 1024 * 1024;
constexpr int kMaxPlsIndex = 10000;
constexpr long kCurlCaptureTimeoutSeconds = 30L;
constexpr long kCurlLowSpeedLimitBytes = 1L;
constexpr long kCurlLowSpeedTimeSeconds = 30L;

bool HasScheme(const std::string& value, const char* scheme) {
    std::string prefix = std::string(scheme) + "://";
    return ToLowerAscii(value).rfind(prefix, 0) == 0;
}

bool IsAbsoluteLocalPath(const std::wstring& value) {
    if (value.rfind(L"\\\\", 0) == 0 || value.rfind(L"//", 0) == 0) return true;
    if (value.size() >= 3 && iswalpha(value[0]) && value[1] == L':' &&
        (value[2] == L'\\' || value[2] == L'/')) {
        return true;
    }
    return !value.empty() && (value[0] == L'/' || value[0] == L'\\');
}

std::wstring ParentLocalPath(const std::wstring& value) {
    size_t slash = value.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return value.substr(0, slash);
}

std::wstring JoinLocalPath(const std::wstring& baseFile, const std::wstring& entry) {
    if (IsAbsoluteLocalPath(entry)) return entry;
    std::wstring parent = ParentLocalPath(baseFile);
#ifdef _WIN32
    const wchar_t slash = L'\\';
#else
    const wchar_t slash = L'/';
#endif
    if (!parent.empty() && parent.back() != L'\\' && parent.back() != L'/') parent.push_back(slash);
    return parent + entry;
}

std::wstring ResolvePlaylistEntry(const std::wstring& playlistPath, const std::wstring& entry) {
    if (IsRemoteMediaUrl(entry) || IsAbsoluteLocalPath(entry)) return entry;
    if (IsRemoteMediaUrl(playlistPath)) return ResolveRelativeUrl(playlistPath, entry);
    return JoinLocalPath(playlistPath, entry);
}

std::wstring ResolvePlaylistSidecar(const std::wstring& playlistPath, const std::wstring& entry) {
    std::wstring trimmed = TrimWide(entry);
    if (trimmed.empty()) return {};
    return ResolvePlaylistEntry(playlistPath, trimmed);
}

struct RemoteFetchResult {
    bool ok = false;
    bool truncated = false;
    long long contentLength = 0;
    std::string body;
};

struct CurlGlobalInit {
    CurlGlobalInit() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    ~CurlGlobalInit() {
        curl_global_cleanup();
    }
};

class CurlShareHandle {
public:
    CurlShareHandle() {
        m_locks.resize(CURL_LOCK_DATA_LAST);
        for (auto& lock : m_locks) {
            lock = std::make_unique<std::mutex>();
        }
        m_share = curl_share_init();
        if (!m_share) return;
        curl_share_setopt(m_share, CURLSHOPT_LOCKFUNC, &CurlShareHandle::LockCallback);
        curl_share_setopt(m_share, CURLSHOPT_UNLOCKFUNC, &CurlShareHandle::UnlockCallback);
        curl_share_setopt(m_share, CURLSHOPT_USERDATA, this);
        curl_share_setopt(m_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(m_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(m_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    }
    ~CurlShareHandle() {
        if (m_share) curl_share_cleanup(m_share);
    }
    CURLSH* Get() const { return m_share; }

private:
    static void LockCallback(CURL*, curl_lock_data data, curl_lock_access, void* userPtr) {
        auto* self = static_cast<CurlShareHandle*>(userPtr);
        self->m_locks[data]->lock();
    }
    static void UnlockCallback(CURL*, curl_lock_data data, void* userPtr) {
        auto* self = static_cast<CurlShareHandle*>(userPtr);
        self->m_locks[data]->unlock();
    }

    CURLSH* m_share = nullptr;
    std::vector<std::unique_ptr<std::mutex>> m_locks;
};

CURLSH* GetSharedCurlHandle() {
    static CurlGlobalInit init;
    (void)init;
    static CurlShareHandle share;
    return share.Get();
}

size_t CurlWriteBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* result = static_cast<RemoteFetchResult*>(userdata);
    size_t bytes = size * nmemb;
    if (result->body.size() + bytes > static_cast<size_t>(kMaxCurlOutputBytes)) {
        result->truncated = true;
        return 0;
    }
    result->body.append(ptr, bytes);
    return bytes;
}

size_t CurlWriteStream(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* writeChunk = static_cast<const std::function<bool(const char*, size_t)>*>(userdata);
    size_t bytes = size * nmemb;
    return (*writeChunk)(ptr, bytes) ? bytes : 0;
}

size_t CurlHeaderStream(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* onHeader = static_cast<const std::function<void(const std::string&, const std::string&)>*>(userdata);
    size_t bytes = size * nitems;
    if (onHeader && *onHeader) {
        std::string header(buffer, bytes);
        size_t colon = header.find(':');
        if (colon != std::string::npos) {
            std::string key = header.substr(0, colon);
            std::string value = header.substr(colon + 1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.pop_back();
            size_t valStart = value.find_first_not_of(" \t");
            if (valStart != std::string::npos) value = value.substr(valStart);
            (*onHeader)(key, value);
        } else if (header == "\r\n" || header == "\n") {
            (*onHeader)("", ""); // empty key/value signals end of headers
        }
    }
    return bytes;
}

int ScanCancelXferInfo(void* /*clientp*/, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                       curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    return AppScanCancel.IsCancelled() ? 1 : 0;
}

CURL* CreateCurlHandle(const std::wstring& url, char* errorBuffer, long timeoutSeconds) {
    static CurlGlobalInit init;
    (void)init;
    CURL* curl = curl_easy_init();
    if (!curl) return nullptr;
    std::string urlText = WideToUtf8(url);
    curl_easy_setopt(curl, CURLOPT_URL, urlText.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DLNA-Server/1.7");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_FTP_RESPONSE_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 1L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    if (CURLSH* share = GetSharedCurlHandle()) {
        curl_easy_setopt(curl, CURLOPT_SHARE, share);
    }
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ScanCancelXferInfo);
    return curl;
}

RemoteFetchResult CurlCapture(const std::wstring& url, bool headOnly, bool listOnly) {
    RemoteFetchResult result;
    char errorBuffer[CURL_ERROR_SIZE] = {};
    CURL* curl = CreateCurlHandle(url, errorBuffer, kCurlCaptureTimeoutSeconds);
    if (!curl) {
        LogPrint(L"Remote content unavailable: libcurl handle creation failed.");
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    if (headOnly) curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    if (listOnly) curl_easy_setopt(curl, CURLOPT_DIRLISTONLY, 1L);

    CURLcode code = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    if (result.truncated) {
        LogPrint(L"[remote:parse] Remote content truncated after %d bytes: %ls", kMaxCurlOutputBytes, RedactUrlForLog(url).c_str());
    } else if (code == CURLE_OK) {
        curl_off_t length = -1;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
        result.contentLength = length > 0 ? static_cast<long long>(length) : 0;
        result.ok = true;
    } else {
        if (responseCode == 401 || responseCode == 403 || code == CURLE_LOGIN_DENIED) {
            LogPrint(L"[remote:auth] Remote content unavailable: authentication failed for %ls", RedactUrlForLog(url).c_str());
        } else {
            LogPrint(L"[remote:network] Remote content unavailable: %hs", errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
        }
    }
    curl_easy_cleanup(curl);
    return result;
}

bool CurlStream(const std::wstring& url,
                bool useRange,
                long long startByte,
                long long endByte,
                const std::function<bool(const char*, size_t)>& writeChunk,
                const std::vector<std::string>& reqHeaders,
                const std::function<void(const std::string&, const std::string&)>& onHeader) {
    char errorBuffer[CURL_ERROR_SIZE] = {};
    CURL* curl = CreateCurlHandle(url, errorBuffer, 0L);
    if (!curl) {
        LogPrint(L"Remote content unavailable: libcurl handle creation failed.");
        return false;
    }

    std::string rangeText;
    if (useRange) {
        rangeText = std::to_string(startByte) + "-" + std::to_string(endByte);
        curl_easy_setopt(curl, CURLOPT_RANGE, rangeText.c_str());
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& header : reqHeaders) {
        chunk = curl_slist_append(chunk, header.c_str());
    }
    if (chunk) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    if (onHeader) {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CurlHeaderStream);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &onHeader);
    }

    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kCurlLowSpeedLimitBytes);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kCurlLowSpeedTimeSeconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeChunk);
    CURLcode code = curl_easy_perform(curl);
    
    if (chunk) {
        curl_slist_free_all(chunk);
    }
    
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        LogPrint(L"Remote content unavailable: %hs", errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
        return false;
    }
return true;
}

std::string ReadLocalTextFile(const std::wstring& path) {
#ifdef _WIN32
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return {};
    std::string text;
    char buffer[4096];
    while (!std::feof(fp)) {
        size_t readCount = std::fread(buffer, 1, sizeof(buffer), fp);
        if (readCount > 0) text.append(buffer, readCount);
        if (readCount < sizeof(buffer) && std::ferror(fp)) break;
    }
    std::fclose(fp);
    return text;
#else
    std::ifstream file(WideToUtf8(path), std::ios::binary);
    if (!file) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
#endif
}

std::string ReadSourceText(const std::wstring& source, bool* ok = nullptr) {
    if (IsRemoteMediaUrl(source)) {
        RemoteFetchResult fetch = CurlCapture(source, false, false);
        if (ok) *ok = fetch.ok;
        return fetch.body;
    }
    if (ok) *ok = true;
    return ReadLocalTextFile(source);
}

std::wstring TitleFromEntry(const std::wstring& location) {
    std::wstring stem = SourceStemName(location);
    return stem.empty() ? SourceDisplayName(location) : stem;
}

std::string UnquotePlaylistValue(std::string value) {
    value = TrimAscii(value);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::vector<PlaylistEntry> ParseM3u(const std::wstring& playlistPath, const std::string& text) {
    std::vector<PlaylistEntry> entries;
    std::istringstream stream(text);
    std::string line;
    std::wstring pendingTitle;
    std::wstring pendingSubtitle;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.empty()) continue;

        if (trimmed.rfind("#EXTINF:", 0) == 0) {
            size_t comma = trimmed.find(',');
            if (comma != std::string::npos && comma + 1 < trimmed.size()) {
                pendingTitle = Utf8ToWide(TrimAscii(trimmed.substr(comma + 1)));
            }
            continue;
        }

        if (trimmed.rfind("#DLNA-SUBTITLE:", 0) == 0) {
            pendingSubtitle = ResolvePlaylistSidecar(playlistPath, Utf8ToWide(trimmed.substr(15)));
            continue;
        }

        if (trimmed.rfind("#EXTVLCOPT:sub-file=", 0) == 0) {
            pendingSubtitle = ResolvePlaylistSidecar(playlistPath, Utf8ToWide(trimmed.substr(20)));
            continue;
        }

        if (trimmed[0] == '#') continue;

        std::wstring location = ResolvePlaylistEntry(playlistPath, Utf8ToWide(trimmed));
        std::wstring title = pendingTitle.empty() ? TitleFromEntry(location) : pendingTitle;
        entries.push_back({ location, title, pendingSubtitle });
        pendingTitle.clear();
        pendingSubtitle.clear();
    }
    if (!pendingTitle.empty() || !pendingSubtitle.empty()) {
        // trailing EXTINF or subtitle directive after last URI
        // apply to last entry so playlist metadata is not lost
        if (!entries.empty()) {
            if (!pendingTitle.empty()) {
                entries.back().title = pendingTitle;
            }
            if (!pendingSubtitle.empty()) {
                entries.back().subtitlePath = pendingSubtitle;
            }
        }
    }
    return entries;
}

struct PlsEntryBuilder {
    std::wstring file;
    std::wstring title;
};

std::vector<PlaylistEntry> ParsePls(const std::wstring& playlistPath, const std::string& text) {
    std::vector<PlsEntryBuilder> parsed;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '[' || trimmed[0] == '#') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLowerAscii(TrimAscii(trimmed.substr(0, eq)));
        std::string value = UnquotePlaylistValue(trimmed.substr(eq + 1));
        size_t digitsAt = std::string::npos;
        for (size_t i = 0; i < key.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(key[i]))) {
                digitsAt = i;
                break;
            }
        }
        if (digitsAt == std::string::npos) continue;
        int index = 0;
        try {
            index = std::stoi(key.substr(digitsAt));
        } catch (...) {
            continue;
        }

        if (index <= 0 || index > kMaxPlsIndex) {
            LogPrint(L"[remote:parse] Ignoring out-of-range PLS index %d in %ls", index, RedactUrlForLog(playlistPath).c_str());
            continue;
        }
        if (parsed.size() < static_cast<size_t>(index)) {
            parsed.resize(static_cast<size_t>(index));
        }

        if (key.rfind("file", 0) == 0) parsed[static_cast<size_t>(index - 1)].file = Utf8ToWide(value);
        else if (key.rfind("title", 0) == 0) parsed[static_cast<size_t>(index - 1)].title = Utf8ToWide(value);
    }

    std::vector<PlaylistEntry> entries;
    for (const auto& entry : parsed) {
        if (entry.file.empty()) continue;
        std::wstring location = ResolvePlaylistEntry(playlistPath, entry.file);
        std::wstring title = entry.title.empty() ? TitleFromEntry(location) : entry.title;
        entries.push_back({ location, title, L"" });
    }
    return entries;
}

bool IsSupportedScheme(const std::string& value) {
    return HasScheme(value, "ftp") || HasScheme(value, "ftps") ||
           HasScheme(value, "http") || HasScheme(value, "https");
}

std::wstring StripQueryExtensionSource(const std::wstring& value) {
    std::string text = UrlWithoutQueryOrFragment(WideToUtf8(value));
    return Utf8ToWide(text);
}

std::wstring ChildUrl(const std::wstring& directoryUrl, const std::wstring& childName);

std::string ParseUnixListName(const std::string& trimmed) {
    size_t pos = 0;
    int fields = 0;
    while (pos < trimmed.size() && fields < 8) {
        while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
        while (pos < trimmed.size() && !std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
        ++fields;
    }
    while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
    return pos < trimmed.size() ? trimmed.substr(pos) : std::string();
}

bool TryParseDosListLine(const std::string& trimmed, std::string& outName, bool& outIsDirectory) {
    size_t pos = 0;
    auto skipDigits = [&](size_t count) -> bool {
        if (pos + count > trimmed.size()) return false;
        for (size_t i = 0; i < count; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(trimmed[pos + i]))) return false;
        }
        pos += count;
        return true;
    };
    if (!skipDigits(2) || pos >= trimmed.size() || trimmed[pos] != '-') return false;
    ++pos;
    if (!skipDigits(2) || pos >= trimmed.size() || trimmed[pos] != '-') return false;
    ++pos;
    if (!(skipDigits(2) || skipDigits(4))) return false;
    if (pos >= trimmed.size() || trimmed[pos] != ' ') return false;

    size_t afterDate = trimmed.find_first_not_of(' ', pos);
    if (afterDate == std::string::npos) return false;
    size_t afterTime = trimmed.find_first_of(' ', afterDate);
    if (afterTime == std::string::npos) return false;
    std::string timeToken = trimmed.substr(afterDate, afterTime - afterDate);
    if (timeToken.find(':') == std::string::npos) return false;

    size_t afterTimeSpace = trimmed.find_first_not_of(' ', afterTime);
    if (afterTimeSpace == std::string::npos) return false;

    const std::string dirMarker = "<DIR>";
    bool isDirectory = trimmed.compare(afterTimeSpace, dirMarker.size(), dirMarker) == 0;

    size_t nameStart;
    if (isDirectory) {
        nameStart = trimmed.find_first_not_of(' ', afterTimeSpace + dirMarker.size());
    } else {
        size_t sizeEnd = trimmed.find_first_of(' ', afterTimeSpace);
        if (sizeEnd == std::string::npos) return false;
        std::string sizeToken = trimmed.substr(afterTimeSpace, sizeEnd - afterTimeSpace);
        if (sizeToken.empty() || sizeToken.find_first_not_of("0123456789") != std::string::npos) return false;
        nameStart = trimmed.find_first_not_of(' ', sizeEnd);
    }
    if (nameStart == std::string::npos) return false;

    outName = trimmed.substr(nameStart);
    outIsDirectory = isDirectory;
    return true;
}

RemoteDirectoryEntry ClassifyRemoteDirectoryEntry(const std::wstring& baseUrl, const std::string& line) {
    std::string trimmed = TrimAscii(line);

    std::string dosName;
    bool dosIsDirectory = false;
    if (TryParseDosListLine(trimmed, dosName, dosIsDirectory)) {
        std::wstring name = Utf8ToWide(dosName);
        std::wstring child = ChildUrl(baseUrl, name);
        return { name, child, dosIsDirectory };
    }

    const bool detailDirectory = !trimmed.empty() && trimmed[0] == 'd';
    const bool detailFile = !trimmed.empty() && trimmed[0] == '-';
    if ((detailDirectory || detailFile) && trimmed.find(' ') != std::string::npos) {
        std::string parsedName = ParseUnixListName(trimmed);
        if (!parsedName.empty()) trimmed = parsedName;
    }
    bool slashDirectory = !trimmed.empty() && trimmed.back() == '/';
    if (slashDirectory) trimmed.pop_back();
    std::wstring name = Utf8ToWide(trimmed);
    std::wstring child = ChildUrl(baseUrl, name);
    const bool likelyDirectory = detailDirectory || slashDirectory;
    return { name, child, likelyDirectory };
}

std::string ParseUrlForJoin(const std::wstring& directoryUrl) {
    return UrlWithoutQueryOrFragment(WideToUtf8(directoryUrl));
}

std::wstring ChildUrl(const std::wstring& directoryUrl, const std::wstring& childName) {
    std::string base = ParseUrlForJoin(directoryUrl);
    if (base.empty() || base.back() != '/') base.push_back('/');
    return Utf8ToWide(base + UrlEncodePathSegment(WideToUtf8(childName)));
}
}

std::string UrlWithoutQueryOrFragment(const std::string& value) {
    size_t end = value.find_first_of("?#");
    return end == std::string::npos ? value : value.substr(0, end);
}

std::string UrlEncodePathSegment(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch == '%' && i + 2 < value.size() &&
            std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            out.push_back('%');
            out.push_back(value[i + 1]);
            out.push_back(value[i + 2]);
            i += 2;
            continue;
        }
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0f]);
        }
    }
    return out;
}

std::wstring ResolveRelativeUrl(const std::wstring& baseUrl, const std::wstring& relativeUrl) {
    if (IsRemoteMediaUrl(relativeUrl)) return relativeUrl;

    std::string base = UrlWithoutQueryOrFragment(WideToUtf8(baseUrl));
    // remove filename part to get base directory
    size_t slash = base.find_last_of('/');
    std::string baseDir;
    if (slash != std::string::npos && slash > 7) { // >7 to keep scheme://host/
        baseDir = base.substr(0, slash + 1);
    } else {
        baseDir = base;
        if (!baseDir.empty() && baseDir.back() != '/') baseDir += '/';
    }

    std::string relFull = WideToUtf8(relativeUrl);
    // RFC 3986 section 3 4 and 3 5 the query and fragment components are
    // opaque to path processing and must be split off before any path
    // segment work happens they are reattached byte for byte at the end
    // never percent re encoded here since a signed CDN URL token lives in
    // this exact byte sequence and any transformation invalidates it
    size_t querySplit = relFull.find_first_of("?#");
    std::string rel = querySplit == std::string::npos ? relFull : relFull.substr(0, querySplit);
    std::string relQueryAndFragment = querySplit == std::string::npos ? std::string() : relFull.substr(querySplit);

    // remove leading slash if present (absolute-path relative to scheme://host)
    if (!rel.empty() && rel[0] == '/') {
        // keep scheme://host/ from base
        size_t hostEnd = base.find('/', 8); // after scheme://host
        if (hostEnd != std::string::npos) {
            baseDir = base.substr(0, hostEnd + 1);
            rel = rel.substr(1);
        } else {
            baseDir = base + "/";
            rel = rel.substr(1);
        }
    }

    // Strip scheme://host/ from baseDir before path-segment processing.
    // The scheme+host is reconstructed separately at lines 679-684, and
    // including it in segments causes the scheme (e.g., "https:") to be
    // re-encoded via UrlEncodePathSegment as "https%3A".
    std::string baseDirPath = baseDir;
    {
        size_t sd = baseDirPath.find("://");
        if (sd != std::string::npos) {
            size_t he = baseDirPath.find('/', sd + 3);
            if (he != std::string::npos) {
                baseDirPath = baseDirPath.substr(he + 1);
            } else {
                baseDirPath.clear();
            }
        }
    }
    // split baseDir path segments
    std::vector<std::string> segments;
    std::stringstream baseParts(baseDirPath);
    std::string seg;
    while (std::getline(baseParts, seg, '/')) {
        if (!seg.empty()) segments.push_back(seg);
    }

    // process relative segments (query and fragment already removed above)
    std::stringstream relParts(rel);
    while (std::getline(relParts, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!segments.empty()) segments.pop_back();
        } else {
            segments.push_back(seg);
        }
    }

    // reconstruct path
    std::string result;
    // rebuild scheme://host/
    size_t schemeEnd = base.find("://");
    size_t hostSlash = base.find('/', schemeEnd != std::string::npos ? schemeEnd + 3 : 0);
    if (schemeEnd != std::string::npos) {
        std::string schemeHost = (hostSlash != std::string::npos) ? base.substr(0, hostSlash + 1) : base + "/";
        result = schemeHost;
    }

    for (size_t i = 0; i < segments.size(); ++i) {
        result += UrlEncodePathSegment(segments[i]);
        if (i + 1 < segments.size()) result += '/';
    }

    // reattach the original query and fragment unchanged per RFC 3986 5 3
    result += relQueryAndFragment;

    return Utf8ToWide(result);
}

std::string RewriteHlsManifestUrisToAbsolute(const std::wstring& manifestUrl, const std::string& manifestText) {
    if (!IsRemoteMediaUrl(manifestUrl)) return manifestText;
    std::string result;
    result.reserve(manifestText.size() + 1024);
    std::istringstream input(manifestText);
    std::string line;
    while (std::getline(input, line)) {
        // trim trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // skip tags and blank lines
        if (line.empty() || line[0] == '#') {
            result += line + "\n";
            continue;
        }
        // line is a URI — resolve against manifest URL
        std::wstring resolved = ResolveRelativeUrl(manifestUrl, Utf8ToWide(line));
        std::string resolvedUtf8 = WideToUtf8(resolved);
        // Append the resolved URI (with original fragment/query if the line had it)
        result += resolvedUtf8 + "\n";
    }
    return result;
}

bool IsHlsManifestText(const std::string& text) {
    // RFC 8216: HLS-specific tags all start with literal "#EXT-X-"
    // Plain M3U/IPTV compilation playlists only use #EXTM3U and #EXTINF
    // Finding one "#EXT-X-" line is a reliable signal this is a single
    // adaptive-bitrate stream not a compilation of separate items
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.rfind("#EXT-X-", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool IsRecognizedPlaylistText(const std::wstring& path, const std::string& text) {
    std::string body = text;
    if (body.size() >= 3 &&
        static_cast<unsigned char>(body[0]) == 0xef &&
        static_cast<unsigned char>(body[1]) == 0xbb &&
        static_cast<unsigned char>(body[2]) == 0xbf) {
        body.erase(0, 3);
    }
    body = TrimAscii(body);

    std::wstring ext = SourceExtension(path);
    if (ext == L".pls") {
        return ToLowerAscii(body).rfind("[playlist]", 0) == 0;
    }
    // RFC 8216 section 4 1: a Playlist file MUST begin with the literal
    // tag #EXTM3U as its first line. Content that does not start with
    // this tag is not a playlist -- an error page, redirect stub, or
    // other non-manifest response -- regardless of the .m3u/.m3u8
    // extension the URL happened to end in.
    return body.rfind("#EXTM3U", 0) == 0;
}

FetchedPlaylist FetchPlaylistOnce(const std::wstring& playlistPath) {
    FetchedPlaylist result;
    result.text = ReadSourceText(playlistPath, &result.fetchOk);
    if (result.fetchOk) {
        result.isHls = IsHlsManifestText(result.text);
    }
    return result;
}

HlsManifestFetchResult FetchHlsManifestForServing(const std::wstring& manifestUrl) {
    HlsManifestFetchResult result;
    bool fetchOk = true;
    std::string text = ReadSourceText(manifestUrl, &fetchOk);
    if (!fetchOk) {
        LogPrint(L"[remote:network] HLS manifest unavailable for serving: %ls", RedactUrlForLog(manifestUrl).c_str());
        return result;
    }
    result.fetchOk = true;
    result.text = RewriteHlsManifestUrisToAbsolute(manifestUrl, text);
    return result;
}

bool IsRemoteMediaUrl(const std::wstring& value) {
    return IsSupportedScheme(WideToUtf8(value));
}

bool IsNetworkShareUrl(const std::wstring& value) {
    std::string text = WideToUtf8(value);
    return HasScheme(text, "ftp") || HasScheme(text, "ftps");
}

bool IsRemovedSmbSourcePath(const std::wstring& value) {
    // SMB support was removed: libcurl's smb:// backend is SMB1/CIFS-only
    // (no SMB2/3 dialect), which most current SMB servers refuse, and has no
    // directory-listing capability at all for any dialect. This helper only
    // exists to detect and reject a leftover smb:// entry from an existing
    // config.ini with an explicit log message instead of a silent failure.
    std::string text = WideToUtf8(value);
    return HasScheme(text, "smb") || HasScheme(text, "smbs");
}

std::wstring RedactUrlForLog(const std::wstring& value) {
    std::string text = WideToUtf8(value);
    size_t schemeEnd = text.find("://");
    if (schemeEnd == std::string::npos) return value;
    size_t authorityStart = schemeEnd + 3;
    size_t authorityEnd = text.find_first_of("/?#", authorityStart);
    size_t at = text.find('@', authorityStart);
    if (at == std::string::npos || (authorityEnd != std::string::npos && at > authorityEnd)) {
        return value;
    }
    std::string redacted = text.substr(0, authorityStart) + "[redacted]@" + text.substr(at + 1);
    return Utf8ToWide(redacted);
}

std::wstring SourceExtension(const std::wstring& value) {
    std::wstring clean = StripQueryExtensionSource(value);
    size_t slash = clean.find_last_of(L"\\/");
    size_t dot = clean.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    return ToLowerWide(clean.substr(dot));
}

bool IsPlaylistSourcePath(const std::wstring& value) {
    std::wstring ext = SourceExtension(value);
    return ext == L".m3u" || ext == L".m3u8" || ext == L".pls";
}

std::wstring SourceDisplayName(const std::wstring& value) {
    std::wstring clean = StripQueryExtensionSource(value);
    while (!clean.empty() && (clean.back() == L'/' || clean.back() == L'\\')) clean.pop_back();
    size_t slash = clean.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? clean : clean.substr(slash + 1);
    return name.empty() ? value : name;
}

std::wstring SourceStemName(const std::wstring& value) {
    std::wstring name = SourceDisplayName(value);
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) return name;
    return name.substr(0, dot);
}

std::vector<PlaylistEntry> ParseFetchedPlaylistText(const std::wstring& playlistPath, const std::string& fetchedText) {
    std::string text = fetchedText;
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }

    std::wstring ext = SourceExtension(playlistPath);
    if (ext == L".pls") return ParsePls(playlistPath, text);
    return ParseM3u(playlistPath, text);
}

std::vector<PlaylistEntry> LoadPlaylistEntries(const std::wstring& playlistPath, bool* fetchFailed) {
    bool fetchOk = true;
    std::string text = ReadSourceText(playlistPath, &fetchOk);
    if (fetchFailed) *fetchFailed = !fetchOk;
    return ParseFetchedPlaylistText(playlistPath, text);
}

std::vector<RemoteDirectoryEntry> ListRemoteDirectory(const std::wstring& directoryUrl) {
    if (!IsNetworkShareUrl(directoryUrl)) {
        if (IsRemoteMediaUrl(directoryUrl)) {
            LogPrint(L"[remote:parse] HTTP directory listing is not supported: %ls", RedactUrlForLog(directoryUrl).c_str());
        }
        return {};
    }
    std::wstring url = directoryUrl;
    if (!url.empty() && url.back() != L'/') url.push_back(L'/');
    RemoteFetchResult fetch = CurlCapture(url, false, true);
    if (!fetch.ok) return {};
    if (fetch.body.empty()) {
        LogPrint(L"Remote directory listing empty: %ls", RedactUrlForLog(url).c_str());
    }
    std::string text = fetch.body;

    std::vector<RemoteDirectoryEntry> entries;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed == "." || trimmed == ".." || trimmed.rfind("total ", 0) == 0) continue;

        RemoteDirectoryEntry entry = ClassifyRemoteDirectoryEntry(url, trimmed);
        if (entry.name.empty() || entry.name == L"." || entry.name == L"..") continue;
        entries.push_back(entry);
    }
    return entries;
}

long long ProbeRemoteContentLength(const std::wstring& url) {
    if (!IsRemoteMediaUrl(url)) return 0;
    
    auto& limiter = GetRemoteProbeLimiter();
    limiter.Acquire();
    
    auto cleanup = ScopeGuard([&]() { limiter.Release(); });
    return CurlCapture(url, true, false).contentLength;
}

namespace {
    AdaptiveConcurrencyLimiter g_remoteProbeLimiter(4);
}

AdaptiveConcurrencyLimiter& GetRemoteProbeLimiter() {
    return g_remoteProbeLimiter;
}

bool StreamRemoteContent(const std::wstring& url,
                         bool useRange,
                         long long startByte,
                         long long endByte,
                         const std::function<bool(const char*, size_t)>& writeChunk,
                         const std::vector<std::string>& reqHeaders,
                         const std::function<void(const std::string&, const std::string&)>& onHeader) {
    if (!IsRemoteMediaUrl(url)) return false;
    return CurlStream(url, useRange, startByte, endByte, writeChunk, reqHeaders, onHeader);
}
