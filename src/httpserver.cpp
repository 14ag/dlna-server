#include "httpserver.h"
#include "log.h"
#include "config.h"
#include "dlna_utils.h"
#include "ipwhitelist.h"
#include "contentdirectory.h"
#include "media_sources.h"
#include "netutils.h"
#include "network_sources.h"
#include "upnp_eventing.h"
#include "../resources/resource.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shlwapi.h>
#include <climits>
#include <sstream>

#define HTTP_BUF_SIZE 8192

namespace {
constexpr size_t kMaxSoapBodyBytes = 1024 * 1024;

void SendAll(SOCKET s, const char* data, int len) {
    while (len > 0) {
        int sent = send(s, data, len, 0);
        if (sent == SOCKET_ERROR || sent == 0) return;
        data += sent;
        len -= sent;
    }
}

void SendAll(SOCKET s, const std::string& str) {
    SendAll(s, str.c_str(), static_cast<int>(str.size()));
}

bool TrySendAll(SOCKET s, const char* data, size_t len) {
    while (len > 0) {
        int chunk = len > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(len);
        int sent = send(s, data, chunk, 0);
        if (sent == SOCKET_ERROR || sent == 0) return false;
        data += sent;
        len -= static_cast<size_t>(sent);
    }
    return true;
}

struct ScopedHandle {
    explicit ScopedHandle(HANDLE h) : handle(h) {}
    ~ScopedHandle() {
        if (valid()) CloseHandle(handle);
    }
    bool valid() const { return handle != INVALID_HANDLE_VALUE && handle != NULL; }
    HANDLE get() const { return handle; }

    HANDLE handle;
};

void SetSocketTimeouts(SOCKET s) {
    constexpr DWORD kDefaultTimeoutMs = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&kDefaultTimeoutMs), sizeof(kDefaultTimeoutMs));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&kDefaultTimeoutMs), sizeof(kDefaultTimeoutMs));
}

void SetSocketStreamTimeouts(SOCKET s) {
    constexpr DWORD kStreamTimeoutMs = 60000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&kStreamTimeoutMs), sizeof(kStreamTimeoutMs));
}

void SetSocketKeepAliveIdleTimeout(SOCKET s) {
    constexpr DWORD kKeepAliveIdleTimeoutMs = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&kKeepAliveIdleTimeoutMs), sizeof(kKeepAliveIdleTimeoutMs));
}

void SetSocketNoDelay(SOCKET s) {
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
}

std::string ConnectionHeader(bool keepAlive) {
    return keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
}

bool ShouldKeepAlive(const std::string& req) {
    std::string connectionValue = ToLowerAscii(FindHeaderValueCaseInsensitive(req, "Connection"));
    if (connectionValue == "close") return false;
    if (connectionValue == "keep-alive") return true;
    // HTTP 1 1 default is keep-alive unless client says close
    // HTTP 1 0 default is close unless client says keep-alive
    // We check the first line for version
    size_t firstLineEnd = req.find("\r\n");
    if (firstLineEnd == std::string::npos) return false;
    std::string firstLine = req.substr(0, firstLineEnd);
    size_t lastSpace = firstLine.rfind(' ');
    if (lastSpace == std::string::npos) return false;
    std::string version = firstLine.substr(lastSpace + 1);
    return version.find("1.1") != std::string::npos;
}

std::string SplitRequestTarget(const std::string& requestTarget) {
    const size_t query = requestTarget.find('?');
    return query == std::string::npos ? requestTarget : requestTarget.substr(0, query);
}

bool ValidateHostHeader(const std::string& host) {
    if (host.empty()) return false;
    for (unsigned char ch : host) {
        if (ch <= 32 || ch == '/' || ch == '\\') return false;
    }
    return true;
}

bool ReadHttpRequestHeaders(SOCKET s, std::string& req) {
    req.clear();
    char buffer[HTTP_BUF_SIZE];
    constexpr size_t kMaxHeaderBytes = 64 * 1024;
    while (req.find("\r\n\r\n") == std::string::npos) {
        int bytesRead = recv(s, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) return false;
        req.append(buffer, static_cast<size_t>(bytesRead));
        if (req.size() > kMaxHeaderBytes) return false;
    }
    return true;
}

int IconResourceForPath(const std::string& path) {
    if (path == "/icons/server_icon_48.png") return IDR_SERVER_ICON_48;
    if (path == "/icons/server_icon_120.png") return IDR_SERVER_ICON_120;
    if (path == "/icons/server_icon_256.png") return IDR_SERVER_ICON_256;
    return 0;
}

bool LoadServerIconPng(int resourceId, std::string& bytes) {
    HRSRC res = FindResourceW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res) return false;
    HGLOBAL loaded = LoadResource(GetModuleHandleW(NULL), res);
    if (!loaded) return false;
    DWORD size = SizeofResource(GetModuleHandleW(NULL), res);
    void* data = LockResource(loaded);
    if (!data || size == 0) return false;
    bytes.assign(static_cast<const char*>(data), static_cast<size_t>(size));
    return true;
}

} // namespace

struct WorkData {
    HttpServer* pServer;
    SOCKET s;
    std::string ip;
};

namespace {
SOCKET CreateListenSocket(int family, int port) {
    SOCKET listenSocket = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        LogPrint(L"HTTP listen socket creation failed family=%d err=%d", family, WSAGetLastError());
        return INVALID_SOCKET;
    }

    // exclusive flag stops a second process from silently binding over
    // this port while this process still owns it
    // a plain reuseaddr flag on windows would allow that and route
    // connections to either listener with no guarantee which one wins
    BOOL exclusive = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    if (family == AF_INET6) {
        DWORD v6Only = 1;
        setsockopt(listenSocket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6Only), sizeof(v6Only));
    }

    if (family == AF_INET) {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            LogPrint(L"HTTP listen bind failed family=%d port=%d err=%d", family, port, WSAGetLastError());
            closesocket(listenSocket);
            return INVALID_SOCKET;
        }
    } else {
        sockaddr_in6 addr6 = {};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(static_cast<u_short>(port));
        addr6.sin6_addr = in6addr_any;

        if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&addr6), sizeof(addr6)) == SOCKET_ERROR) {
            LogPrint(L"HTTP listen bind failed family=%d port=%d err=%d", family, port, WSAGetLastError());
            closesocket(listenSocket);
            return INVALID_SOCKET;
        }
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        LogPrint(L"HTTP listen call failed family=%d port=%d err=%d", family, port, WSAGetLastError());
        closesocket(listenSocket);
        return INVALID_SOCKET;
    }

    return listenSocket;
}
}

HttpServer& HttpServer::Get() {
    static HttpServer instance;
    return instance;
}

HttpServer::HttpServer()
    : m_running(false),
      m_hAcceptThread(NULL),
      m_listenSocketV4(INVALID_SOCKET),
      m_listenSocketV6(INVALID_SOCKET),
      m_threadPool(NULL),
      m_cleanupGroup(NULL) {
}

bool HttpServer::Start(int port) {
    if (m_running.load()) return true;

    m_listenSocketV4 = CreateListenSocket(AF_INET, port);
    m_listenSocketV6 = CreateListenSocket(AF_INET6, port);

    if (m_listenSocketV4 == INVALID_SOCKET && m_listenSocketV6 == INVALID_SOCKET) {
        LogPrint(L"HTTP server failed to bind on both address families for port %d", port);
        return false;
    }
    if (m_listenSocketV4 == INVALID_SOCKET) {
        LogPrint(L"HTTP server bound ipv6 only on port %d ipv4 was unavailable", port);
    }
    if (m_listenSocketV6 == INVALID_SOCKET) {
        LogPrint(L"HTTP server bound ipv4 only on port %d ipv6 was unavailable", port);
    }

    m_threadPool = CreateThreadpool(NULL);
    if (!m_threadPool) {
        Stop();
        return false;
    }

    SetThreadpoolThreadMinimum(m_threadPool, 4);
    SetThreadpoolThreadMaximum(m_threadPool, 64);
    InitializeThreadpoolEnvironment(&m_cbe);
    m_cleanupGroup = CreateThreadpoolCleanupGroup();
    if (!m_cleanupGroup) {
        Stop();
        return false;
    }

    SetThreadpoolCallbackPool(&m_cbe, m_threadPool);
    SetThreadpoolCallbackCleanupGroup(&m_cbe, m_cleanupGroup, NULL);

    m_running.store(true);
    m_hAcceptThread = CreateThread(NULL, 0, AcceptThreadWorker, this, 0, NULL);
    if (!m_hAcceptThread) {
        Stop();
        return false;
    }

    return true;
}

void HttpServer::Stop() {
    if (!m_running.load() && m_listenSocketV4 == INVALID_SOCKET && m_listenSocketV6 == INVALID_SOCKET) return;

    m_running.store(false);
    AppEvents.ClearSubscriptions();

    if (m_hAcceptThread) {
        WaitForSingleObject(m_hAcceptThread, INFINITE);
        CloseHandle(m_hAcceptThread);
        m_hAcceptThread = NULL;
    }

    if (m_listenSocketV4 != INVALID_SOCKET) {
        closesocket(m_listenSocketV4);
        m_listenSocketV4 = INVALID_SOCKET;
    }

    if (m_listenSocketV6 != INVALID_SOCKET) {
        closesocket(m_listenSocketV6);
        m_listenSocketV6 = INVALID_SOCKET;
    }

    if (m_cleanupGroup) {
        CloseThreadpoolCleanupGroupMembers(m_cleanupGroup, FALSE, NULL);
        CloseThreadpoolCleanupGroup(m_cleanupGroup);
        m_cleanupGroup = NULL;
    }

    if (m_threadPool) {
        CloseThreadpool(m_threadPool);
        m_threadPool = NULL;
    }
}

DWORD WINAPI HttpServer::AcceptThreadWorker(LPVOID lpParam) {
    HttpServer* pThis = reinterpret_cast<HttpServer*>(lpParam);

    while (pThis->m_running.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);

        int socketCount = 0;
        if (pThis->m_listenSocketV4 != INVALID_SOCKET) {
            FD_SET(pThis->m_listenSocketV4, &readfds);
            ++socketCount;
        }
        if (pThis->m_listenSocketV6 != INVALID_SOCKET) {
            FD_SET(pThis->m_listenSocketV6, &readfds);
            ++socketCount;
        }
        if (socketCount == 0) {
            break;
        }

        timeval tv = { 1, 0 };
        int result = select(0, &readfds, NULL, NULL, &tv);
        if (result <= 0) {
            continue;
        }

        SOCKET readySockets[] = { pThis->m_listenSocketV4, pThis->m_listenSocketV6 };
        for (SOCKET listenSocket : readySockets) {
            if (listenSocket == INVALID_SOCKET || !FD_ISSET(listenSocket, &readfds)) {
                continue;
            }

            sockaddr_storage clientAddr = {};
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(listenSocket, reinterpret_cast<SOCKADDR*>(&clientAddr), &clientAddrLen);
            if (clientSocket == INVALID_SOCKET) {
                continue;
            }
            SetSocketTimeouts(clientSocket);
            SetSocketNoDelay(clientSocket);

            std::string clientIP = NormalizeIpLiteral(SockaddrToLiteral(reinterpret_cast<const SOCKADDR*>(&clientAddr)));

            if (!IPWhitelist::Get().IsAllowed(clientIP)) {
                LogPrint(L"Blocked connection from %hs", clientIP.c_str());
                static const char* forbidden = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                send(clientSocket, forbidden, static_cast<int>(strlen(forbidden)), 0);
                closesocket(clientSocket);
                continue;
            }

            WorkData* wd = new WorkData{ pThis, clientSocket, clientIP };
            PTP_WORK work = CreateThreadpoolWork(WorkerCallback, wd, &pThis->m_cbe);
            if (!work) {
                closesocket(clientSocket);
                delete wd;
                continue;
            }

            SubmitThreadpoolWork(work);
        }
    }

    return 0;
}

void CALLBACK HttpServer::WorkerCallback(PTP_CALLBACK_INSTANCE, PVOID Context, PTP_WORK Work) {
    WorkData* wd = reinterpret_cast<WorkData*>(Context);
    wd->pServer->HandleClient(wd->s, wd->ip);
    closesocket(wd->s);
    delete wd;
    CloseThreadpoolWork(Work);
}

void HttpServer::HandleClient(SOCKET clientSocket, const std::string& clientIP) {
    char buf[HTTP_BUF_SIZE];
    bool firstRequest = true;

    while (m_running.load()) {
        if (!firstRequest) {
            SetSocketKeepAliveIdleTimeout(clientSocket);
        }
        firstRequest = false;

        std::string req;
        if (!ReadHttpRequestHeaders(clientSocket, req)) return;

        bool keepAlive = ShouldKeepAlive(req);

        size_t firstLineEnd = req.find("\r\n");
        if (firstLineEnd == std::string::npos) return;
        std::string firstLine = req.substr(0, firstLineEnd);

        size_t space1 = firstLine.find(' ');
        size_t space2 = firstLine.rfind(' ');
        if (space1 == std::string::npos || space2 == std::string::npos || space2 <= space1) return;

        std::string method = firstLine.substr(0, space1);
        std::string path = SplitRequestTarget(firstLine.substr(space1 + 1, space2 - space1 - 1));
        const int listenPort = AppConfig.GetPort();
        if (AppConfig.IsDebugLogEnabled()) {
            LogPrint(L"HTTP request: src=%hs method=%hs path=%hs", clientIP.c_str(), method.c_str(), path.c_str());
        }

        std::string hostUrl = FindHeaderValueCaseInsensitive(req, "Host");
        if (!hostUrl.empty() && !ValidateHostHeader(hostUrl)) {
            SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            return;
        }
        if (hostUrl.empty()) {
            sockaddr_storage localAddr = {};
            int localAddrLen = sizeof(localAddr);
            if (getsockname(clientSocket, reinterpret_cast<SOCKADDR*>(&localAddr), &localAddrLen) == 0) {
                hostUrl = SockaddrToHostPort(reinterpret_cast<const SOCKADDR*>(&localAddr), listenPort);
            } else {
                hostUrl = clientIP + ":" + std::to_string(listenPort);
            }
        }

        const bool sendBody = method != "HEAD";

        auto sendText = [&](const std::string& status, const std::string& type, const std::string& body) {
            std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: " + type +
                                   "\r\nContent-Length: " + std::to_string(body.size()) +
                                   "\r\n" + ConnectionHeader(keepAlive) + "\r\n";
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
                sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetDeviceDescriptionXML());
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

            int iconResourceId = IconResourceForPath(path);
            if (iconResourceId != 0) {
                std::string body;
                if (!LoadServerIconPng(iconResourceId, body)) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                sendText("200 OK", "image/png", body);
                if (!keepAlive) return;
                continue;
            }

            if (path.rfind("/media/", 0) == 0) {
                int fileId = -1;
                if (!TryParseIntStrict(path.substr(7), fileId) || fileId < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                MediaItem item = AppMedia.GetItem(fileId);
                if (item.id != -1) {
                    if (IsRemoteMediaUrl(item.path)) {
                        long long fileSize = item.sizeBytes > 0 ? item.sizeBytes : ProbeRemoteContentLength(item.path);
                        std::string rangeHeader = FindHeaderValueCaseInsensitive(req, "Range");
                        bool hasKnownSize = fileSize > 0;
                        HttpByteRange parsedRange;
                        if (hasKnownSize) {
                            parsedRange = ParseHttpRangeHeader(rangeHeader, fileSize);
                            if (!parsedRange.satisfiable) {
                                std::string rangeResp = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(fileSize) + "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                                SendAll(clientSocket, rangeResp);
                                return;
                            }
                        } else if (!rangeHeader.empty()) {
                            SendAll(clientSocket, "HTTP/1.1 416 Range Not Satisfiable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }

                        bool isPartial = hasKnownSize && parsedRange.requested;
                        long long startByte = hasKnownSize ? parsedRange.start : 0;
                        long long endByte = hasKnownSize ? parsedRange.end : 0;
                        long long contentLength = hasKnownSize ? (endByte - startByte + 1) : 0;
                        std::string mime = WideToUtf8(item.mimeType);

                        std::stringstream headers;
                        headers << "HTTP/1.1 " << (isPartial ? "206 Partial Content" : "200 OK") << "\r\n";
                        if (isPartial) {
                            headers << "Content-Range: bytes " << startByte << "-" << endByte << "/" << fileSize << "\r\n";
                        }
                        headers << "Content-Type: " << mime << "\r\n";
                        if (hasKnownSize) {
                            headers << "Content-Length: " << contentLength << "\r\n"
                                    << "Accept-Ranges: bytes\r\n";
                        } else {
                            headers << "Accept-Ranges: none\r\n";
                        }
                        headers << ConnectionHeader(keepAlive)
                                << "transferMode.dlna.org: Streaming\r\n"
                                << "contentFeatures.dlna.org: " << BuildContentFeaturesForExtension(SourceExtension(item.path), item.mimeType, hasKnownSize) << "\r\n"
                                << "\r\n";

                        SendAll(clientSocket, headers.str());
                        if (!sendBody) return;

                        SetSocketStreamTimeouts(clientSocket);
                        bool remoteOk = StreamRemoteContent(item.path, isPartial, startByte, endByte, [&](const char* data, size_t length) {
                            const char* p = data;
                            size_t remaining = length;
                            while (remaining > 0) {
                                int chunk = remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
                                int sent = send(clientSocket, p, chunk, 0);
                                if (sent == SOCKET_ERROR || sent == 0) return false;
                                p += sent;
                                remaining -= static_cast<size_t>(sent);
                            }
                            return m_running.load();
                        });
                        if (!remoteOk || !keepAlive) return;
                        continue;
                    }

                    ScopedHandle hFile(CreateFileW(item.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    if (hFile.valid()) {
                        LARGE_INTEGER fileSize = {};
                        if (!GetFileSizeEx(hFile.get(), &fileSize)) {
                            SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }

                        HttpByteRange parsedRange = ParseHttpRangeHeader(FindHeaderValueCaseInsensitive(req, "Range"), fileSize.QuadPart);
                        if (!parsedRange.satisfiable) {
                            std::string rangeResp = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(fileSize.QuadPart) + "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                            SendAll(clientSocket, rangeResp);
                            return;
                        }

                        long long startByte = parsedRange.start;
                        long long endByte = parsedRange.end;
                        bool isPartial = parsedRange.requested;

                        long long contentLength = endByte - startByte + 1;
                        std::string mime = WideToUtf8(item.mimeType);

                        std::stringstream headers;
                        if (isPartial) {
                            headers << "HTTP/1.1 206 Partial Content\r\n";
                            headers << "Content-Range: bytes " << startByte << "-" << endByte << "/" << fileSize.QuadPart << "\r\n";
                        } else {
                            headers << "HTTP/1.1 200 OK\r\n";
                        }

                        headers << "Content-Type: " << mime << "\r\n"
                                << "Content-Length: " << contentLength << "\r\n"
                                << "Accept-Ranges: bytes\r\n"
                                << ConnectionHeader(keepAlive)
                                << "transferMode.dlna.org: Streaming\r\n"
                                << "contentFeatures.dlna.org: " << BuildContentFeaturesForExtension(SourceExtension(item.path), item.mimeType, true) << "\r\n"
                                << "\r\n";

                        std::string headStr = headers.str();
                        SendAll(clientSocket, headStr);

                        if (!sendBody) {
                            return;
                        }

                        SetSocketStreamTimeouts(clientSocket);
                        LARGE_INTEGER movePos = {};
                        movePos.QuadPart = startByte;
                        SetFilePointerEx(hFile.get(), movePos, NULL, FILE_BEGIN);

                        long long remaining = contentLength;
                        char fileBuf[65536];
                        DWORD bytesToRead = 0;
                        DWORD bytesReadFile = 0;

                        while (remaining > 0 && m_running.load()) {
                            bytesToRead = (remaining > static_cast<long long>(sizeof(fileBuf))) ? static_cast<DWORD>(sizeof(fileBuf)) : static_cast<DWORD>(remaining);
                            if (!ReadFile(hFile.get(), fileBuf, bytesToRead, &bytesReadFile, NULL) || bytesReadFile == 0) {
                                break;
                            }

                            if (!TrySendAll(clientSocket, fileBuf, bytesReadFile)) break;
                            remaining -= bytesReadFile;
                        }
                        if (remaining > 0 || !keepAlive) return;
                        continue;
                    }
                }

                SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }

            // serve subtitle file by video item id
            // the subtitle path was stored during media scan; it may be a
            // local filesystem path or a remote URL resolved from a
            // playlist's #DLNA-SUBTITLE:/#EXTVLCOPT:sub-file= line
            if (path.rfind("/subtitle/", 0) == 0) {
                int fileId = -1;
                if (!TryParseIntStrict(path.substr(10), fileId) || fileId < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                MediaItem item = AppMedia.GetItem(fileId);

                if (item.id != -1 && !item.subtitlePath.empty() && IsRemoteMediaUrl(item.subtitlePath)) {
                    std::string subMime = SubtitleMimeForExtension(SourceExtension(item.subtitlePath));
                    long long subtitleSize = ProbeRemoteContentLength(item.subtitlePath);
                    bool hasKnownSize = subtitleSize > 0;

                    std::stringstream subHeaders;
                    subHeaders << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: " << subMime << "\r\n";
                    if (hasKnownSize) {
                        subHeaders << "Content-Length: " << subtitleSize << "\r\n";
                    }
                    subHeaders << "Accept-Ranges: none\r\n"
                               << "Connection: close\r\n\r\n";
                    SendAll(clientSocket, subHeaders.str());
                    if (!sendBody) {
                        return;
                    }

                    bool streamed = StreamRemoteContent(item.subtitlePath, false, 0, 0, [&](const char* data, size_t length) {
                        return TrySendAll(clientSocket, data, length) && m_running.load();
                    });
                    if (!streamed) {
                        LogPrint(L"Remote subtitle unavailable: %ls", RedactUrlForLog(item.subtitlePath).c_str());
                    }
                    return;
                }

                if (item.id != -1 && !item.subtitlePath.empty()) {
                    ScopedHandle hFile(CreateFileW(item.subtitlePath.c_str(), GENERIC_READ,
                        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    if (hFile.valid()) {
                        std::string subMime = SubtitleMimeForExtension(SourceExtension(item.subtitlePath));

                        LARGE_INTEGER fileSize = {};
                        if (!GetFileSizeEx(hFile.get(), &fileSize)) {
                            SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }
                        long long totalBytes = fileSize.QuadPart;

                        std::stringstream subHeaders;
                        subHeaders << "HTTP/1.1 200 OK\r\n"
                                   << "Content-Type: " << subMime << "\r\n"
                                   << "Content-Length: " << totalBytes << "\r\n"
                                   << "Accept-Ranges: none\r\n"
                                   << "Connection: close\r\n"
                                   << "\r\n";
                        SendAll(clientSocket, subHeaders.str());
                        if (!sendBody) {
                            return;
                        }

                        char subBuf[16384];
                        DWORD subRead = 0;
                        while (m_running.load()) {
                            if (!ReadFile(hFile.get(), subBuf, sizeof(subBuf), &subRead, NULL) ||
                                subRead == 0)
                                break;
                            if (!TrySendAll(clientSocket, subBuf, subRead)) break;
                        }
                        return;
                    }
                }
                SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }

            if (path.rfind("/albumart/", 0) == 0) {
                int fileId = -1;
                if (!TryParseIntStrict(path.substr(10), fileId) || fileId < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                MediaItem item = AppMedia.GetItem(fileId);
                if (item.id != -1 && !item.albumArtPath.empty()) {
                    ScopedHandle hFile(CreateFileW(item.albumArtPath.c_str(), GENERIC_READ,
                        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    if (hFile.valid()) {
                        LARGE_INTEGER fileSize = {};
                        if (!GetFileSizeEx(hFile.get(), &fileSize)) {
                            SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }
                        std::stringstream artHeaders;
                        artHeaders << "HTTP/1.1 200 OK\r\n"
                                   << "Content-Type: " << WideToUtf8(item.albumArtMime.empty() ? L"image/jpeg" : item.albumArtMime) << "\r\n"
                                   << "Content-Length: " << fileSize.QuadPart << "\r\n"
                                   << "Accept-Ranges: none\r\n"
                                   << "Connection: close\r\n\r\n";
                        SendAll(clientSocket, artHeaders.str());
                        if (!sendBody) {
                            return;
                        }

                        char artBuf[16384];
                        DWORD artRead = 0;
                        while (m_running.load()) {
                            if (!ReadFile(hFile.get(), artBuf, sizeof(artBuf), &artRead, NULL) || artRead == 0) break;
                            if (!TrySendAll(clientSocket, artBuf, artRead)) break;
                        }
                        return;
                    }
                }
                SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
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
                int r = recv(clientSocket, buf, sizeof(buf), 0);
                if (r <= 0) break;
                body.append(buf, static_cast<size_t>(r));
            }
            if (body.size() < expectedBodyLength) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }

            sendText("200 OK",
                     "text/xml; charset=\"utf-8\"",
                     path == "/upnp/control/connection_manager"
                         ? AppContent.HandleConnectionManagerControl(body)
                         : AppContent.HandleContentDirectoryControl(body, hostUrl));
            if (!keepAlive) return;
            continue;
        }

        static const char* badRequest = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        SendAll(clientSocket, badRequest, static_cast<int>(strlen(badRequest)));
        return;
    }
}
