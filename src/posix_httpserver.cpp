#include "httpserver.h"
#include "http_common.h"
#include "config.h"
#include "contentdirectory.h"
#include "dlna_utils.h"
#include "ipwhitelist.h"
#include "log.h"
#include "media_sources.h"
#include "netutils.h"
#include "network_sources.h"
#include "upnp_eventing.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fstream>
#include <limits.h>
#include <mutex>
#include <sstream>
#include <vector>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// macOS lacks MSG_NOSIGNAL; use SO_NOSIGPIPE socket option instead
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace {
constexpr size_t kMaxClientThreads = 64;

class ScopedFd {
public:
    explicit ScopedFd(int fd = -1) : m_fd(fd) {}
    ~ScopedFd() { reset(); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    int get() const { return m_fd; }
    int release() {
        int fd = m_fd;
        m_fd = -1;
        return fd;
    }
    void reset(int fd = -1) {
        if (m_fd >= 0) close(m_fd);
        m_fd = fd;
    }

private:
    int m_fd;
};

int CreateListenSocket(int family, int port) {
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    if (family == AF_INET6) setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
    if (family == AF_INET) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close(fd); return -1; }
    } else {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(static_cast<uint16_t>(port));
        addr.sin6_addr = in6addr_any;
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { close(fd); return -1; }
    }
    if (listen(fd, SOMAXCONN) != 0) { close(fd); return -1; }
    return fd;
}

void SetSocketTimeouts(int fd) {
    timeval timeout{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void SetSocketStreamTimeouts(int fd) {
    timeval timeout{60, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

void SetSocketKeepAliveIdleTimeout(int fd) {
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

void SetSocketNoDelay(int fd) {
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

void SendAll(int fd, const std::string& bytes) {
    const char* data = bytes.data();
    size_t remaining = bytes.size();
    while (remaining > 0) {
        ssize_t sent = send(fd, data, remaining, MSG_NOSIGNAL);
        if (sent <= 0) return;
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }
}

bool TrySendAll(int fd, const char* data, size_t remaining) {
    while (remaining > 0) {
        ssize_t sent = send(fd, data, remaining, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

std::string IconFileNameForPath(const std::string& path) {
    if (path == "/icons/server_icon_48.png") return "server_icon_48.png";
    if (path == "/icons/server_icon_120.png") return "server_icon_120.png";
    if (path == "/icons/server_icon_256.png") return "server_icon_256.png";
    return {};
}

bool ReadIconFile(const std::string& path, std::string& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    bytes = ss.str();
    return !bytes.empty();
}

std::string ExecutableDirectory() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::string exe(path);
        const size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash);
    }
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len > 0) {
        path[len] = '\0';
        std::string exe(path);
        const size_t slash = exe.find_last_of('/');
        if (slash != std::string::npos) return exe.substr(0, slash);
    }
#endif
    return ".";
}

bool LoadServerIconPng(const std::string& fileName, std::string& bytes) {
    if (fileName.empty()) return false;
    const std::string exeDir = ExecutableDirectory();
    std::vector<std::string> candidates = {
        std::string(DLNA_RESOURCE_DIR) + "/" + fileName,
        exeDir + "/" + fileName,
        exeDir + "/../share/dlna-server/icons/" + fileName,
        exeDir + "/../Resources/" + fileName,
    };
    for (const auto& candidate : candidates) {
        if (ReadIconFile(candidate, bytes)) return true;
    }
    return false;
}

}

HttpServer& HttpServer::Get() {
    static HttpServer instance;
    return instance;
}

HttpServer::HttpServer() : m_running(false), m_listenSocketV4(-1), m_listenSocketV6(-1) {
}

bool HttpServer::Start(int port) {
    if (m_running) return true;
    m_listenSocketV4 = CreateListenSocket(AF_INET, port);
    m_listenSocketV6 = CreateListenSocket(AF_INET6, port);
    if (m_listenSocketV4 < 0 && m_listenSocketV6 < 0) return false;
    m_running = true;
    if (m_listenSocketV4 >= 0) m_threads.emplace_back(&HttpServer::AcceptLoop, this, m_listenSocketV4);
    if (m_listenSocketV6 >= 0) m_threads.emplace_back(&HttpServer::AcceptLoop, this, m_listenSocketV6);
    return true;
}

void HttpServer::Stop() {
    m_running = false;
    AppEvents.ClearSubscriptions();
    if (m_listenSocketV4 >= 0) { shutdown(m_listenSocketV4, SHUT_RDWR); close(m_listenSocketV4); m_listenSocketV4 = -1; }
    if (m_listenSocketV6 >= 0) { shutdown(m_listenSocketV6, SHUT_RDWR); close(m_listenSocketV6); m_listenSocketV6 = -1; }
    for (auto& thread : m_threads) if (thread.joinable()) thread.join();
    m_threads.clear();
    std::vector<ClientThread> clients;
    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        clients.swap(m_clientThreads);
    }
    for (auto& client : clients) if (client.thread.joinable()) client.thread.join();
}

void HttpServer::ReapFinishedClientThreads() {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    auto it = m_clientThreads.begin();
    while (it != m_clientThreads.end()) {
        if (it->done && it->done->load()) {
            if (it->thread.joinable()) it->thread.join();
            it = m_clientThreads.erase(it);
        } else {
            ++it;
        }
    }
}

void HttpServer::AcceptLoop(int listenSocket) {
    while (m_running) {
        ReapFinishedClientThreads();
        sockaddr_storage remote{};
        socklen_t len = sizeof(remote);
        int client = accept(listenSocket, reinterpret_cast<sockaddr*>(&remote), &len);
        if (client < 0) continue;
        SetSocketTimeouts(client);
        SetSocketNoDelay(client);
#ifdef SO_NOSIGPIPE
        { int on = 1; setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)); }
#endif
        std::string clientIp = NormalizeIpLiteral(SockaddrToLiteral(reinterpret_cast<sockaddr*>(&remote)));
        if (!IPWhitelist::Get().IsAllowed(clientIp)) {
            LogPrint(L"Blocked connection from %hs", clientIp.c_str());
            static const char* forbidden = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            send(client, forbidden, strlen(forbidden), MSG_NOSIGNAL);
            close(client);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(m_clientMutex);
            if (m_clientThreads.size() >= kMaxClientThreads) {
                static const char* busy = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                send(client, busy, strlen(busy), MSG_NOSIGNAL);
                close(client);
                continue;
            }
            auto done = std::make_shared<std::atomic<bool>>(false);
            m_clientThreads.push_back({
                std::thread([this, client, clientIp, done]() {
                    HandleClient(client, clientIp);
                    done->store(true);
                }),
                done
            });
        }
    }
}

void HttpServer::HandleClient(int clientSocket, const std::string& clientIp) {
ScopedFd client(clientSocket);
    char buf[kHttpBufSize];
    bool firstRequest = true;

    while (m_running.load()) {
        if (!firstRequest) {
            SetSocketKeepAliveIdleTimeout(clientSocket);
        }
        firstRequest = false;

        std::string req;
        if (!ReadHttpRequestHeaders([clientSocket](char* buf, int len) { return static_cast<int>(recv(clientSocket, buf, static_cast<size_t>(len), 0)); }, req)) return;

        bool keepAlive = ShouldKeepAlive(req);

        const size_t firstLineEnd = req.find("\r\n");
        if (firstLineEnd == std::string::npos) return;
        const std::string firstLine = req.substr(0, firstLineEnd);
        const size_t space1 = firstLine.find(' ');
        const size_t space2 = firstLine.rfind(' ');
        if (space1 == std::string::npos || space2 <= space1) return;
        const std::string method = firstLine.substr(0, space1);
        const std::string path = SplitRequestTarget(firstLine.substr(space1 + 1, space2 - space1 - 1));
        const int listenPort = AppConfig.GetPort();
        if (AppConfig.IsDebugLogEnabled()) {
            LogPrint(L"HTTP request: src=%hs method=%hs path=%hs", clientIp.c_str(), method.c_str(), path.c_str());
        }
        std::string hostUrl = FindHeaderValueCaseInsensitive(req, "Host");
        if (!hostUrl.empty() && !ValidateHostHeader(hostUrl)) {
            SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        if (hostUrl.empty()) hostUrl = "127.0.0.1:" + std::to_string(listenPort);

        const bool sendBody = method != "HEAD";
        auto sendText = [&](const std::string& status, const std::string& type, const std::string& body) {
            std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + ConnectionHeader(keepAlive) + "\r\n";
            if (sendBody) response += body;
            SendAll(clientSocket, response);
        };

        if (method == "SUBSCRIBE" || method == "UNSUBSCRIBE") {
            SendAll(clientSocket, AppEvents.HandleEventSubscription(method, path, req));
            if (!keepAlive) return;
            continue;
        }

        if (method == "GET" || method == "HEAD") {
            if (path == "/description.xml") {
                sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetDeviceDescriptionXML(hostUrl));
                if (!keepAlive) return;
                continue;
            }
            if (path == "/ContentDirectory.xml") {
                sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetContentDirectoryXML());
                if (!keepAlive) return;
                continue;
            }
            if (path == "/ConnectionManager.xml") {
                sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetConnectionManagerXML());
                if (!keepAlive) return;
                continue;
            }
            if (std::string iconFileName = IconFileNameForPath(path); !iconFileName.empty()) {
                std::string body;
                if (!LoadServerIconPng(iconFileName, body)) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                sendText("200 OK", "image/png", body);
                if (!keepAlive) return;
                continue;
            }
            if (path.rfind("/media/", 0) == 0) {
                int mediaId = -1;
                if (!TryParseIntStrict(path.substr(7), mediaId) || mediaId < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                MediaItem item = AppMedia.GetItem(mediaId);
                if (item.id != -1) {
                    LogPrint(L"HTTP media request: id=%d path=%ls", mediaId, item.path.c_str());
                    if (item.mimeType == L"video/mpegurl") {
                        // mirrors the windows httpserver cpp branch exactly
                        // see that file for the full rationale comment
                        HlsManifestFetchResult manifest = FetchHlsManifestForServing(item.path);
                        if (!manifest.fetchOk) {
                            SendAll(clientSocket, "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }
                        std::stringstream hlsHeaders;
                        hlsHeaders << "HTTP/1.1 200 OK\r\n"
                                   << "Content-Type: video/mpegurl\r\n"
                                   << "Content-Length: " << manifest.text.size() << "\r\n"
                                   << "Accept-Ranges: none\r\n"
                                   << ConnectionHeader(keepAlive)
                                   << "transferMode.dlna.org: Streaming\r\n"
                                   << "contentFeatures.dlna.org: " << BuildHlsContentFeatures() << "\r\n"
                                   << "\r\n";
                        SendAll(clientSocket, hlsHeaders.str());
                        if (sendBody) {
                            SendAll(clientSocket, manifest.text);
                        }
                        if (!keepAlive) return;
                        continue;
                    }
                    if (IsRemoteMediaUrl(item.path)) {
                    long long fileSize = item.sizeBytes > 0 ? item.sizeBytes : ProbeRemoteContentLength(item.path);
                    std::string rangeHeader = FindHeaderValueCaseInsensitive(req, "Range");
                    bool hasKnownSize = fileSize > 0;
                    HttpByteRange parsedRange;
                    if (hasKnownSize) {
                        parsedRange = ParseHttpRangeHeader(rangeHeader, fileSize);
                        if (!parsedRange.satisfiable) {
                            SendAll(clientSocket, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(fileSize) + "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }
                    }

                    bool partial = hasKnownSize && parsedRange.requested;
                    long long start = hasKnownSize ? parsedRange.start : 0;
                    long long end = hasKnownSize ? parsedRange.end : 0;
                    long long bodyLength = hasKnownSize ? (end - start + 1) : 0;
                    std::stringstream headers;
                    headers << "HTTP/1.1 " << (partial ? "206 Partial Content" : "200 OK") << "\r\n";
                    if (partial) headers << "Content-Range: bytes " << start << "-" << end << "/" << fileSize << "\r\n";
                    headers << "Content-Type: " << WideToUtf8(item.mimeType) << "\r\n";
                    
                    bool spoofSamsung = false;
                    if (!hasKnownSize) {
                        std::string ua = FindHeaderValueCaseInsensitive(req, "User-Agent");
                        if (ua.empty() || ua.find("SEC_HHP_") != std::string::npos) {
                            spoofSamsung = true;
                        }
                    }

                    if (hasKnownSize) {
                        headers << "Content-Length: " << bodyLength << "\r\n"
                                << "Accept-Ranges: bytes\r\n";
                    } else if (spoofSamsung) {
                        headers << "Content-Length: 1073741824\r\n"
                                << "Accept-Ranges: none\r\n";
                    } else {
                        headers << "Accept-Ranges: none\r\n";
                    }
                    headers << ConnectionHeader(keepAlive)
                            << "transferMode.dlna.org: Streaming\r\n"
                            << "contentFeatures.dlna.org: " << BuildContentFeaturesForExtension(SourceExtension(item.path), item.mimeType, hasKnownSize) << "\r\n";

                    std::vector<std::string> proxyReqHeaders;
                    if (FindHeaderValueCaseInsensitive(req, "Icy-MetaData") == "1") {
                        proxyReqHeaders.push_back("Icy-MetaData: 1");
                    }

                    if (method != "GET") {
                        headers << "\r\n";
                        SendAll(clientSocket, headers.str());
                        if (!keepAlive) return;
                        continue;
                    }

                    SetSocketStreamTimeouts(clientSocket);
                    bool headersSent = false;
                    std::string baseHeaders = headers.str();

                    auto onRemoteHeader = [&](const std::string& key, const std::string& value) {
                        if (headersSent) return;
                        if (key.empty() && value.empty()) {
                            baseHeaders += "\r\n";
                            SendAll(clientSocket, baseHeaders);
                            headersSent = true;
                            return;
                        }
                        std::string lowerKey = ToLowerAscii(key);
                        if (lowerKey.find("icy-") == 0) {
                            baseHeaders += key + ": " + value + "\r\n";
                        }
                    };

                    bool remoteOk = StreamRemoteContent(item.path, partial, start, end, [&](const char* data, size_t length) {
                        if (!headersSent) {
                            baseHeaders += "\r\n";
                            SendAll(clientSocket, baseHeaders);
                            headersSent = true;
                        }
                        const char* p = data;
                        size_t remaining = length;
                        while (remaining > 0) {
                            ssize_t sent = send(clientSocket, p, remaining, MSG_NOSIGNAL);
                            if (sent <= 0) return false;
                            p += sent;
                            remaining -= static_cast<size_t>(sent);
                        }
                        return m_running.load();
                    }, proxyReqHeaders, onRemoteHeader);
                    
                    if (!headersSent) {
                        baseHeaders += "\r\n";
                        SendAll(clientSocket, baseHeaders);
                    }

                    if (!remoteOk || !keepAlive) return;
                    continue;
                }
                ScopedFd fd(item.id == -1 ? -1 : open(WideToUtf8(item.path).c_str(), O_RDONLY));
                if (fd.get() < 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                struct stat st{};
                if (fstat(fd.get(), &st) != 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }

                bool hasKnownSize = st.st_size > 0;
                HttpByteRange parsedRange{};
                if (hasKnownSize) {
                    parsedRange = ParseHttpRangeHeader(FindHeaderValueCaseInsensitive(req, "Range"), static_cast<long long>(st.st_size));
                    if (!parsedRange.satisfiable) {
                        SendAll(clientSocket, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(static_cast<long long>(st.st_size)) + "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                        return;
                    }
                }

                bool partial = hasKnownSize && parsedRange.requested;
                long long start = hasKnownSize ? parsedRange.start : 0;
                long long end = hasKnownSize ? parsedRange.end : 0;
                long long bodyLength = hasKnownSize ? (end - start + 1) : 0;
                std::stringstream headers;
                headers << "HTTP/1.1 " << (partial ? "206 Partial Content" : "200 OK") << "\r\n";
                if (partial) headers << "Content-Range: bytes " << start << "-" << end << "/" << static_cast<long long>(st.st_size) << "\r\n";
                headers << "Content-Type: " << WideToUtf8(item.mimeType) << "\r\n";

                if (hasKnownSize) {
                    headers << "Content-Length: " << bodyLength << "\r\n"
                            << "Accept-Ranges: bytes\r\n";
                } else {
                    headers << "Accept-Ranges: none\r\n";
                }
                headers << ConnectionHeader(keepAlive)
                        << "transferMode.dlna.org: Streaming\r\n"
                        << "contentFeatures.dlna.org: " << BuildContentFeaturesForExtension(SourceExtension(item.path), item.mimeType, true) << "\r\n";
                SendAll(clientSocket, headers.str());
                if (method == "GET") {
                    SetSocketStreamTimeouts(clientSocket);
                    lseek(fd.get(), start, SEEK_SET);
                    long long remaining = bodyLength;
                    while (remaining > 0) {
                        ssize_t chunk = read(fd.get(), buf, (std::min)(sizeof(buf), static_cast<size_t>(remaining)));
                        if (chunk <= 0) break;
                        if (!TrySendAll(clientSocket, buf, static_cast<size_t>(chunk))) break;
                        remaining -= chunk;
                    }
                    if (remaining > 0 || !keepAlive) return;
                    continue;
                }
            }
            if (path.rfind("/subtitle/", 0) == 0) {
                int mediaId = -1;
                if (!TryParseIntStrict(path.substr(10), mediaId) || mediaId < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                MediaItem item = AppMedia.GetItem(mediaId);

                if (item.id != -1 && !item.subtitlePath.empty() && IsRemoteMediaUrl(item.subtitlePath)) {
                    std::string subMime = SubtitleMimeForExtension(SourceExtension(item.subtitlePath));
                    long long subtitleSize = ProbeRemoteContentLength(item.subtitlePath);
                    bool hasKnownSize = subtitleSize > 0;

                    std::stringstream headers;
                    headers << "HTTP/1.1 200 OK\r\n"
                            << "Content-Type: " << subMime << "\r\n";
                    if (hasKnownSize) {
                        headers << "Content-Length: " << subtitleSize << "\r\n";
                    }
                    headers << "Accept-Ranges: none\r\nConnection: close\r\n\r\n";
                    SendAll(clientSocket, headers.str());
                    if (sendBody) {
                        bool streamed = StreamRemoteContent(item.subtitlePath, false, 0, 0, [&](const char* data, size_t length) {
                            return TrySendAll(clientSocket, data, length) && m_running.load();
                        });
                        if (!streamed) {
                            LogPrint(L"Remote subtitle unavailable: %ls", RedactUrlForLog(item.subtitlePath).c_str());
                        }
                    }
                    return;
                }

                ScopedFd fd(item.id == -1 || item.subtitlePath.empty() ? -1 : open(WideToUtf8(item.subtitlePath).c_str(), O_RDONLY));
                if (fd.get() < 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                struct stat st{};
                if (fstat(fd.get(), &st) != 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                std::string subMime = SubtitleMimeForExtension(SourceExtension(item.subtitlePath));
                std::stringstream headers;
                headers << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: " << subMime << "\r\n"
                        << "Content-Length: " << static_cast<long long>(st.st_size) << "\r\n"
                        << "Accept-Ranges: none\r\nConnection: close\r\n\r\n";
                SendAll(clientSocket, headers.str());
                if (sendBody) {
                    while (true) {
                        ssize_t chunk = read(fd.get(), buf, sizeof(buf));
                        if (chunk <= 0) break;
                        if (!TrySendAll(clientSocket, buf, static_cast<size_t>(chunk))) break;
                    }
                }
                return;
            }
            if (path.rfind("/albumart/", 0) == 0) {
                int mediaId = -1;
                if (!TryParseIntStrict(path.substr(10), mediaId) || mediaId < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                MediaItem item = AppMedia.GetItem(mediaId);
                ScopedFd fd(item.id == -1 || item.albumArtPath.empty() ? -1 : open(WideToUtf8(item.albumArtPath).c_str(), O_RDONLY));
                if (fd.get() < 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                struct stat st{};
                if (fstat(fd.get(), &st) != 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                std::stringstream headers;
                headers << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: " << WideToUtf8(item.albumArtMime.empty() ? L"image/jpeg" : item.albumArtMime) << "\r\n"
                        << "Content-Length: " << static_cast<long long>(st.st_size) << "\r\n"
                        << "Accept-Ranges: none\r\n"
                        << "Connection: close\r\n\r\n";
                SendAll(clientSocket, headers.str());
                if (sendBody) {
                    while (true) {
                        ssize_t chunk = read(fd.get(), buf, sizeof(buf));
                        if (chunk <= 0) break;
                        if (!TrySendAll(clientSocket, buf, static_cast<size_t>(chunk))) break;
                    }
                }
                return;
            }
            SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        if (method == "POST" && (path == "/upnp/control/content_directory" || path == "/upnp/control/connection_manager")) {
            const size_t headersEnd = req.find("\r\n\r\n");
            std::string body = headersEnd == std::string::npos ? std::string() : req.substr(headersEnd + 4);
            const std::string lengthText = FindHeaderValueCaseInsensitive(req, "Content-Length");
            if (lengthText.empty()) {
                LogPrint(L"Content-Length header required for SOAP POST.");
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            long long contentLength = 0;
            if (!TryParseNonNegativeLongLong(lengthText, contentLength) ||
                contentLength < 0 ||
                static_cast<unsigned long long>(contentLength) > kMaxSoapBodyBytes) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            const size_t expectedBodyLength = static_cast<size_t>(contentLength);
            while (body.size() < expectedBodyLength) {
                ssize_t r = recv(clientSocket, buf, sizeof(buf), 0);
                if (r <= 0) break;
                body.append(buf, static_cast<size_t>(r));
            }
            if (body.size() < expectedBodyLength) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            sendText("200 OK", "text/xml; charset=\"utf-8\"", path == "/upnp/control/connection_manager"
                ? AppContent.HandleConnectionManagerControl(body)
                : AppContent.HandleContentDirectoryControl(body, hostUrl));
            if (!keepAlive) return;
            continue;
        }
        SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        return;
    }
}
