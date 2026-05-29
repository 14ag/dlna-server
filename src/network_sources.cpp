#include "network_sources.h"
#include "dlna_utils.h"
#include "netutils.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
constexpr int kMaxCurlOutputBytes = 4 * 1024 * 1024;

std::wstring TrimWide(const std::wstring& value);

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::wstring ToLowerWideCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool HasScheme(const std::string& value, const char* scheme) {
    std::string prefix = std::string(scheme) + "://";
    return ToLowerCopy(value).rfind(prefix, 0) == 0;
}

bool IsAbsoluteLocalPath(const std::wstring& value) {
    if (value.rfind(L"\\\\", 0) == 0 || value.rfind(L"//", 0) == 0) return true;
    if (value.size() >= 3 && std::iswalpha(value[0]) && value[1] == L':' &&
        (value[2] == L'\\' || value[2] == L'/')) {
        return true;
    }
    return !value.empty() && (value[0] == L'/' || value[0] == L'\\');
}

std::string UrlWithoutQueryOrFragment(const std::string& value) {
    size_t end = value.find_first_of("?#");
    return end == std::string::npos ? value : value.substr(0, end);
}

std::string ParentUrl(const std::string& value) {
    std::string clean = UrlWithoutQueryOrFragment(value);
    if (!clean.empty() && clean.back() == '/') clean.pop_back();
    size_t slash = clean.find_last_of('/');
    if (slash == std::string::npos) return value;
    return clean.substr(0, slash + 1);
}

std::wstring ParentLocalPath(const std::wstring& value) {
    size_t slash = value.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return value.substr(0, slash);
}

std::string UrlEncodePathSegment(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : value) {
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

std::wstring JoinUrl(const std::wstring& baseUrl, const std::wstring& entry) {
    if (IsRemoteMediaUrl(entry)) return entry;
    std::string parent = ParentUrl(WideToUtf8(baseUrl));
    std::string relative = WideToUtf8(entry);
    std::stringstream joined;
    joined << parent;
    std::stringstream parts(relative);
    std::string part;
    bool first = true;
    while (std::getline(parts, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (!first && joined.str().back() != '/') joined << '/';
        joined << UrlEncodePathSegment(part);
        first = false;
    }
    return Utf8ToWide(joined.str());
}

std::wstring ResolvePlaylistEntry(const std::wstring& playlistPath, const std::wstring& entry) {
    if (IsRemoteMediaUrl(entry) || IsAbsoluteLocalPath(entry)) return entry;
    if (IsRemoteMediaUrl(playlistPath)) return JoinUrl(playlistPath, entry);
    return JoinLocalPath(playlistPath, entry);
}

std::wstring ResolvePlaylistSidecar(const std::wstring& playlistPath, const std::wstring& entry) {
    std::wstring trimmed = TrimWide(entry);
    if (trimmed.empty()) return {};
    return ResolvePlaylistEntry(playlistPath, trimmed);
}

#ifdef _WIN32
std::wstring QuoteProcessArg(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashCount = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashCount;
        } else if (ch == L'"') {
            out.append((slashCount * 2) + 1, L'\\');
            out.push_back(ch);
            slashCount = 0;
        } else {
            out.append(slashCount, L'\\');
            slashCount = 0;
            out.push_back(ch);
        }
    }
    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring BuildCurlProcessLine(const std::vector<std::wstring>& args) {
    std::vector<std::wstring> allArgs = { L"curl.exe", L"--globoff", L"--silent", L"--fail" };
    allArgs.insert(allArgs.end(), args.begin(), args.end());

    std::wstring commandLine;
    for (const auto& arg : allArgs) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += QuoteProcessArg(arg);
    }
    return commandLine;
}

bool RunCurlWithReader(const std::vector<std::wstring>& args,
                       const std::function<bool(const char*, size_t)>& readChunk) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = NULL;
    HANDLE writePipe = NULL;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring commandLine = BuildCurlProcessLine(args);
    BOOL started = CreateProcessW(nullptr,
                                  commandLine.data(),
                                  nullptr,
                                  nullptr,
                                  TRUE,
                                  CREATE_NO_WINDOW,
                                  nullptr,
                                  nullptr,
                                  &startup,
                                  &process);
    CloseHandle(writePipe);
    if (!started) {
        CloseHandle(readPipe);
        return false;
    }

    bool keepGoing = true;
    char buffer[65536];
    DWORD readBytes = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes > 0) {
        if (keepGoing) {
            keepGoing = readChunk(buffer, static_cast<size_t>(readBytes));
            if (!keepGoing) TerminateProcess(process.hProcess, 1);
        }
    }

    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return keepGoing && exitCode == 0;
}
#else
std::vector<std::string> BuildCurlArgvText(const std::vector<std::wstring>& args) {
    std::vector<std::string> allArgs = { "curl", "--globoff", "--silent", "--fail" };
    for (const auto& arg : args) allArgs.push_back(WideToUtf8(arg));
    return allArgs;
}

bool RunCurlWithReader(const std::vector<std::wstring>& args,
                       const std::function<bool(const char*, size_t)>& readChunk) {
    int pipeFd[2] = {-1, -1};
    if (pipe(pipeFd) != 0) return false;

    pid_t child = fork();
    if (child < 0) {
        close(pipeFd[0]);
        close(pipeFd[1]);
        return false;
    }

    if (child == 0) {
        dup2(pipeFd[1], STDOUT_FILENO);
        close(pipeFd[0]);
        close(pipeFd[1]);

        std::vector<std::string> argvText = BuildCurlArgvText(args);
        std::vector<char*> argv;
        argv.reserve(argvText.size() + 1);
        for (auto& arg : argvText) argv.push_back(arg.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipeFd[1]);

    bool keepGoing = true;
    char buffer[65536];
    while (true) {
        ssize_t readBytes = read(pipeFd[0], buffer, sizeof(buffer));
        if (readBytes <= 0) break;
        if (keepGoing) {
            keepGoing = readChunk(buffer, static_cast<size_t>(readBytes));
            if (!keepGoing) kill(child, SIGTERM);
        }
    }

    close(pipeFd[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return keepGoing && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

std::string RunCurlCapture(const std::vector<std::wstring>& args) {
    std::string output;
    bool ok = RunCurlWithReader(args, [&](const char* data, size_t length) {
        if (output.size() < kMaxCurlOutputBytes) {
            size_t allowed = static_cast<size_t>(kMaxCurlOutputBytes) - output.size();
            output.append(data, length < allowed ? length : allowed);
        }
        return true; // Always drain to avoid blocking curl on a full pipe.
    });
    return ok ? output : std::string();
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

std::string ReadSourceText(const std::wstring& source) {
    if (IsRemoteMediaUrl(source)) return RunCurlCapture({ Utf8ToWide("--location"), source });
    return ReadLocalTextFile(source);
}

std::wstring TitleFromEntry(const std::wstring& location) {
    std::wstring stem = SourceStemName(location);
    return stem.empty() ? SourceDisplayName(location) : stem;
}

std::wstring TrimWide(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && std::iswspace(value[start])) ++start;
    size_t end = value.size();
    while (end > start && std::iswspace(value[end - 1])) --end;
    return value.substr(start, end - start);
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
    return entries;
}

std::vector<PlaylistEntry> ParsePls(const std::wstring& playlistPath, const std::string& text) {
    std::map<int, std::wstring> files;
    std::map<int, std::wstring> titles;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed[0] == '[' || trimmed[0] == '#') continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLowerCopy(TrimAscii(trimmed.substr(0, eq)));
        std::string value = TrimAscii(trimmed.substr(eq + 1));
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

        if (key.rfind("file", 0) == 0) files[index] = Utf8ToWide(value);
        else if (key.rfind("title", 0) == 0) titles[index] = Utf8ToWide(value);
    }

    std::vector<PlaylistEntry> entries;
    for (const auto& it : files) {
        std::wstring location = ResolvePlaylistEntry(playlistPath, it.second);
        std::wstring title = titles[it.first].empty() ? TitleFromEntry(location) : titles[it.first];
        entries.push_back({ location, title, L"" });
    }
    return entries;
}

bool IsSupportedScheme(const std::string& value) {
    return HasScheme(value, "smb") || HasScheme(value, "smbs") ||
           HasScheme(value, "ftp") || HasScheme(value, "ftps") ||
           HasScheme(value, "http") || HasScheme(value, "https");
}

std::wstring StripQueryExtensionSource(const std::wstring& value) {
    std::string text = UrlWithoutQueryOrFragment(WideToUtf8(value));
    return Utf8ToWide(text);
}

std::wstring ChildUrl(const std::wstring& directoryUrl, const std::wstring& childName) {
    std::string base = WideToUtf8(directoryUrl);
    if (base.empty() || base.back() != '/') base.push_back('/');
    return Utf8ToWide(base + UrlEncodePathSegment(WideToUtf8(childName)));
}
}

bool IsRemoteMediaUrl(const std::wstring& value) {
    return IsSupportedScheme(WideToUtf8(value));
}

bool IsNetworkShareUrl(const std::wstring& value) {
    std::string text = WideToUtf8(value);
    return HasScheme(text, "smb") || HasScheme(text, "smbs") ||
           HasScheme(text, "ftp") || HasScheme(text, "ftps");
}

std::wstring SourceExtension(const std::wstring& value) {
    std::wstring clean = StripQueryExtensionSource(value);
    size_t slash = clean.find_last_of(L"\\/");
    size_t dot = clean.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    return ToLowerWideCopy(clean.substr(dot));
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

std::vector<PlaylistEntry> LoadPlaylistEntries(const std::wstring& playlistPath) {
    std::string text = ReadSourceText(playlistPath);
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

std::vector<RemoteDirectoryEntry> ListRemoteDirectory(const std::wstring& directoryUrl) {
    if (!IsNetworkShareUrl(directoryUrl)) return {};
    std::wstring url = directoryUrl;
    if (!url.empty() && url.back() != L'/') url.push_back(L'/');
    std::string text = RunCurlCapture({ Utf8ToWide("--list-only"), url });

    std::vector<RemoteDirectoryEntry> entries;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string trimmed = TrimAscii(line);
        if (trimmed.empty() || trimmed == "." || trimmed == "..") continue;

        bool slashDirectory = trimmed.back() == '/';
        if (slashDirectory) trimmed.pop_back();
        std::wstring name = Utf8ToWide(trimmed);
        std::wstring child = ChildUrl(url, name);
        std::wstring ext = SourceExtension(name);
        bool likelyDirectory = slashDirectory || (ext.empty() && !IsPlaylistSourcePath(name));
        entries.push_back({ name, child, likelyDirectory });
    }
    return entries;
}

long long ProbeRemoteContentLength(const std::wstring& url) {
    if (!IsRemoteMediaUrl(url)) return 0;
    std::string headers = RunCurlCapture({ Utf8ToWide("--head"), Utf8ToWide("--location"), url });
    std::istringstream stream(headers);
    std::string line;
    long long size = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string lower = ToLowerCopy(line);
        if (lower.rfind("content-length:", 0) == 0) {
            long long parsed = 0;
            if (TryParseNonNegativeLongLong(TrimAscii(line.substr(15)), parsed)) size = parsed;
        }
    }
    return size;
}

bool StreamRemoteContent(const std::wstring& url,
                         bool useRange,
                         long long startByte,
                         long long endByte,
                         const std::function<bool(const char*, size_t)>& writeChunk) {
    if (!IsRemoteMediaUrl(url)) return false;

    std::vector<std::wstring> args;
    args.push_back(Utf8ToWide("--location"));
    if (useRange) {
        args.push_back(Utf8ToWide("--range"));
        args.push_back(Utf8ToWide(std::to_string(startByte) + "-" + std::to_string(endByte)));
    }
    args.push_back(url);

    bool wrote = false;
    bool ok = RunCurlWithReader(args, [&](const char* data, size_t length) {
        wrote = true;
        return writeChunk(data, length);
    });
    return wrote && ok;
}
