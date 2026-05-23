#include "httpserver.h"
#include "log.h"
#include "config.h"
#include "dlna_utils.h"
#include "ipwhitelist.h"
#include "contentdirectory.h"
#include "media_sources.h"
#include "netutils.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shlwapi.h>
#include <sstream>

#define HTTP_BUF_SIZE 8192

namespace {
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
        return INVALID_SOCKET;
    }

    BOOL reuse = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

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
            closesocket(listenSocket);
            return INVALID_SOCKET;
        }
    } else {
        sockaddr_in6 addr6 = {};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(static_cast<u_short>(port));
        addr6.sin6_addr = in6addr_any;

        if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&addr6), sizeof(addr6)) == SOCKET_ERROR) {
            closesocket(listenSocket);
            return INVALID_SOCKET;
        }
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
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
    if (m_running) return true;

    m_listenSocketV4 = CreateListenSocket(AF_INET, port);
    m_listenSocketV6 = CreateListenSocket(AF_INET6, port);

    if (m_listenSocketV4 == INVALID_SOCKET && m_listenSocketV6 == INVALID_SOCKET) {
        return false;
    }

    m_threadPool = CreateThreadpool(NULL);
    if (!m_threadPool) {
        Stop();
        return false;
    }

    SetThreadpoolThreadMinimum(m_threadPool, 4);
    SetThreadpoolThreadMaximum(m_threadPool, 8);
    InitializeThreadpoolEnvironment(&m_cbe);
    m_cleanupGroup = CreateThreadpoolCleanupGroup();
    if (!m_cleanupGroup) {
        Stop();
        return false;
    }

    SetThreadpoolCallbackPool(&m_cbe, m_threadPool);
    SetThreadpoolCallbackCleanupGroup(&m_cbe, m_cleanupGroup, NULL);

    m_running = true;
    m_hAcceptThread = CreateThread(NULL, 0, AcceptThreadWorker, this, 0, NULL);
    if (!m_hAcceptThread) {
        Stop();
        return false;
    }

    return true;
}

void HttpServer::Stop() {
    if (!m_running && m_listenSocketV4 == INVALID_SOCKET && m_listenSocketV6 == INVALID_SOCKET) return;

    m_running = false;

    if (m_listenSocketV4 != INVALID_SOCKET) {
        closesocket(m_listenSocketV4);
        m_listenSocketV4 = INVALID_SOCKET;
    }

    if (m_listenSocketV6 != INVALID_SOCKET) {
        closesocket(m_listenSocketV6);
        m_listenSocketV6 = INVALID_SOCKET;
    }

    if (m_hAcceptThread) {
        WaitForSingleObject(m_hAcceptThread, INFINITE);
        CloseHandle(m_hAcceptThread);
        m_hAcceptThread = NULL;
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

    while (pThis->m_running) {
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
    int bytesRead = recv(clientSocket, buf, HTTP_BUF_SIZE - 1, 0);
    if (bytesRead <= 0) return;

    buf[bytesRead] = '\0';
    std::string req(buf);

    size_t firstLineEnd = req.find("\r\n");
    if (firstLineEnd == std::string::npos) return;
    std::string firstLine = req.substr(0, firstLineEnd);

    size_t space1 = firstLine.find(' ');
    size_t space2 = firstLine.rfind(' ');
    if (space1 == std::string::npos || space2 == std::string::npos || space2 <= space1) return;

    std::string method = firstLine.substr(0, space1);
    std::string path = firstLine.substr(space1 + 1, space2 - space1 - 1);
    if (AppConfig.debugLog) {
        LogPrint(L"HTTP request: src=%hs method=%hs path=%hs", clientIP.c_str(), method.c_str(), path.c_str());
    }

    std::string hostUrl = FindHeaderValueCaseInsensitive(req, "Host");
    if (hostUrl.empty()) {
        sockaddr_storage localAddr = {};
        int localAddrLen = sizeof(localAddr);
        if (getsockname(clientSocket, reinterpret_cast<SOCKADDR*>(&localAddr), &localAddrLen) == 0) {
            hostUrl = SockaddrToHostPort(reinterpret_cast<const SOCKADDR*>(&localAddr), AppConfig.port);
        } else {
            hostUrl = clientIP + ":" + std::to_string(AppConfig.port);
        }
    }

    if (method == "GET" || method == "HEAD") {
        const bool sendBody = method == "GET";
        std::string response;
        if (path == "/description.xml") {
            std::string body = AppContent.GetDeviceDescriptionXML();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n";
            if (sendBody) response += body;
            SendAll(clientSocket, response);
            return;
        }

        if (path == "/ContentDirectory.xml") {
            std::string body = AppContent.GetContentDirectoryXML();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n";
            if (sendBody) response += body;
            SendAll(clientSocket, response);
            return;
        }

        if (path == "/ConnectionManager.xml") {
            std::string body = AppContent.GetConnectionManagerXML();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n";
            if (sendBody) response += body;
            SendAll(clientSocket, response);
            return;
        }

        if (path.rfind("/media/", 0) == 0) {
            int fileId = -1;
            if (!TryParseIntStrict(path.substr(7), fileId) || fileId < 0) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            MediaItem item = AppMedia.GetItem(fileId);
            if (item.id != -1) {
                HANDLE hFile = CreateFileW(item.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER fileSize = {};
                    GetFileSizeEx(hFile, &fileSize);

                    HttpByteRange parsedRange = ParseHttpRangeHeader(FindHeaderValueCaseInsensitive(req, "Range"), fileSize.QuadPart);
                    if (!parsedRange.satisfiable) {
                        CloseHandle(hFile);
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
                            << "Connection: close\r\n"
                            << "transferMode.dlna.org: Streaming\r\n"
                            << "contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01700000000000000000000000000000\r\n"
                            << "\r\n";

                    std::string headStr = headers.str();
                    SendAll(clientSocket, headStr);

                    if (!sendBody) {
                        CloseHandle(hFile);
                        return;
                    }

                    LARGE_INTEGER movePos = {};
                    movePos.QuadPart = startByte;
                    SetFilePointerEx(hFile, movePos, NULL, FILE_BEGIN);

                    long long remaining = contentLength;
                    char fileBuf[65536];
                    DWORD bytesToRead = 0;
                    DWORD bytesReadFile = 0;

                    while (remaining > 0 && m_running) {
                        bytesToRead = (remaining > static_cast<long long>(sizeof(fileBuf))) ? static_cast<DWORD>(sizeof(fileBuf)) : static_cast<DWORD>(remaining);
                        if (!ReadFile(hFile, fileBuf, bytesToRead, &bytesReadFile, NULL) || bytesReadFile == 0) {
                            break;
                        }

                        const char* p = fileBuf;
                        DWORD toSend = bytesReadFile;
                        while (toSend > 0) {
                            int sent = send(clientSocket, p, toSend, 0);
                            if (sent == SOCKET_ERROR || sent == 0) goto done_streaming;
                            p += sent;
                            toSend -= sent;
                        }

                        remaining -= bytesReadFile;
                    }
                    done_streaming:
                    CloseHandle(hFile);
                    return;
                }
            }

            response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            SendAll(clientSocket, response);
            return;
        }

        // serve subtitle file by video item id
        // the subtitle path was stored during media scan
        if (path.rfind("/subtitle/", 0) == 0) {
            int fileId = -1;
            if (!TryParseIntStrict(path.substr(10), fileId) || fileId < 0) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            MediaItem item = AppMedia.GetItem(fileId);
            if (item.id != -1 && !item.subtitlePath.empty()) {
                HANDLE hFile = CreateFileW(item.subtitlePath.c_str(), GENERIC_READ,
                    FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    LPCWSTR subExtW = PathFindExtensionW(item.subtitlePath.c_str());
                    std::string subMime = SubtitleMimeForExtension(subExtW);

                    LARGE_INTEGER fileSize = {};
                    GetFileSizeEx(hFile, &fileSize);
                    long long totalBytes = fileSize.QuadPart;

                    std::stringstream subHeaders;
                    subHeaders << "HTTP/1.1 200 OK\r\n"
                               << "Content-Type: " << subMime << "\r\n"
                               << "Content-Length: " << totalBytes << "\r\n"
                               << "Accept-Ranges: bytes\r\n"
                               << "Connection: close\r\n"
                               << "\r\n";
                    SendAll(clientSocket, subHeaders.str());
                    if (!sendBody) {
                        CloseHandle(hFile);
                        return;
                    }

                    // stream subtitle bytes to client
                    char subBuf[16384];
                    DWORD subRead = 0;
                    while (m_running) {
                        if (!ReadFile(hFile, subBuf, sizeof(subBuf), &subRead, NULL) ||
                            subRead == 0)
                            break;
                        const char* p = subBuf;
                        DWORD toSend = subRead;
                        while (toSend > 0) {
                            int sent = send(clientSocket, p, toSend, 0);
                            if (sent == SOCKET_ERROR || sent == 0) goto done_subtitle;
                            p += sent;
                            toSend -= sent;
                        }
                    }
                    done_subtitle:
                    CloseHandle(hFile);
                    return;
                }
            }
            SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            return;
        }

        response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        SendAll(clientSocket, response);
        return;
    }

    if (method == "POST" && path == "/upnp/control/content_directory") {
        size_t headersEnd = req.find("\r\n\r\n");
        std::string body;
        if (headersEnd != std::string::npos) {
            size_t bodyStart = headersEnd + 4;
            body = req.substr(bodyStart);

            std::string contentLengthHeader = FindHeaderValueCaseInsensitive(req, "Content-Length");
            if (!contentLengthHeader.empty()) {
                int contentLength = 0;
                if (!TryParseIntStrict(contentLengthHeader, contentLength) || contentLength < 0) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                while (static_cast<int>(body.length()) < contentLength && m_running) {
                    int r = recv(clientSocket, buf, HTTP_BUF_SIZE, 0);
                    if (r <= 0) break;
                    body.append(buf, r);
                }
                if (static_cast<int>(body.length()) < contentLength) {
                    SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
            }

            std::string browseResp = AppContent.HandleBrowse(body, hostUrl);
            std::string fullResp = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(browseResp.length()) + "\r\nConnection: close\r\n\r\n" + browseResp;
            SendAll(clientSocket, fullResp);
            return;
        }
    }

    static const char* badRequest = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    SendAll(clientSocket, badRequest, static_cast<int>(strlen(badRequest)));
}
