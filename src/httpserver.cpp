#include "httpserver.h"
#include "log.h"
#include "config.h"
#include "ipwhitelist.h"
#include "contentdirectory.h"
#include "media_sources.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>

#define HTTP_BUF_SIZE 8192

struct WorkData {
    HttpServer* pServer;
    SOCKET s;
    std::string ip;
};

HttpServer& HttpServer::Get() {
    static HttpServer instance;
    return instance;
}

HttpServer::HttpServer() : m_running(false), m_hAcceptThread(NULL), m_listenSocket(INVALID_SOCKET), m_threadPool(NULL), m_cleanupGroup(NULL) {
}

bool HttpServer::Start(int port) {
    if (m_running) return true;

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_listenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    m_threadPool = CreateThreadpool(NULL);
    SetThreadpoolThreadMinimum(m_threadPool, 4);
    SetThreadpoolThreadMaximum(m_threadPool, 8);
    InitializeThreadpoolEnvironment(&m_cbe);
    m_cleanupGroup = CreateThreadpoolCleanupGroup();
    SetThreadpoolCallbackPool(&m_cbe, m_threadPool);
    SetThreadpoolCallbackCleanupGroup(&m_cbe, m_cleanupGroup, NULL);

    m_running = true;
    m_hAcceptThread = CreateThread(NULL, 0, AcceptThreadWorker, this, 0, NULL);

    return true;
}

void HttpServer::Stop() {
    if (!m_running) return;
    
    m_running = false;
    
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
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
    HttpServer* pThis = (HttpServer*)lpParam;
    
    while (pThis->m_running) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(pThis->m_listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        
        if (clientSocket == INVALID_SOCKET) {
            break; 
        }

        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), ipBuf, INET_ADDRSTRLEN);
        std::string clientIP(ipBuf);

        // Check Whitelist
        if (!IPWhitelist::Get().IsAllowed(clientIP)) {
            LogPrint(L"Blocked connection from %hs", clientIP.c_str());
            closesocket(clientSocket);
            continue;
        }

        // Package work for threadpool
        WorkData* wd = new WorkData{pThis, clientSocket, clientIP};
        PTP_WORK work = CreateThreadpoolWork(WorkerCallback, wd, &pThis->m_cbe);
        SubmitThreadpoolWork(work);
    }
    return 0;
}

void CALLBACK HttpServer::WorkerCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work) {
    auto wd = (WorkData*)Context;
    wd->pServer->HandleClient(wd->s, wd->ip);
    closesocket(wd->s);
    delete wd;
}

// Very basic HTTP reader to parse first line and headers, and optional body
void HttpServer::HandleClient(SOCKET clientSocket, const std::string& clientIP) {
    char buf[HTTP_BUF_SIZE];
    int bytesRead = recv(clientSocket, buf, HTTP_BUF_SIZE - 1, 0);
    if (bytesRead <= 0) return;

    buf[bytesRead] = '\0';
    std::string req(buf);

    size_t firstLineEnd = req.find("\r\n");
    if (firstLineEnd == std::string::npos) return;
    std::string firstLine = req.substr(0, firstLineEnd);

    size_t space1 = firstLine.find(" ");
    size_t space2 = firstLine.rfind(" ");
    if (space1 == std::string::npos || space2 == std::string::npos) return;

    std::string method = firstLine.substr(0, space1);
    std::string path = firstLine.substr(space1 + 1, space2 - space1 - 1);

    // Host URL for Browse responses
    char hostName[256];
    gethostname(hostName, sizeof(hostName));
    std::string hostUrl = clientIP + ":" + std::to_string(AppConfig.port);

    size_t hostPos = req.find("Host: ");
    if(hostPos != std::string::npos) {
        size_t endHost = req.find("\r\n", hostPos);
        hostUrl = req.substr(hostPos + 6, endHost - hostPos - 6);
    }

    if (method == "GET") {
        std::string response;
        if (path == "/description.xml") {
            std::string body = AppContent.GetDeviceDescriptionXML();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
            send(clientSocket, response.c_str(), response.length(), 0);
        } else if (path == "/ContentDirectory.xml") {
            std::string body = AppContent.GetContentDirectoryXML();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
            send(clientSocket, response.c_str(), response.length(), 0);
        } else if (path == "/ConnectionManager.xml") {
            std::string body = AppContent.GetConnectionManagerXML();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
            send(clientSocket, response.c_str(), response.length(), 0);
        } else if (path.find("/media/") == 0) {
            int fileId = std::stoi(path.substr(7));
            MediaItem item = AppMedia.GetItem(fileId);
            if (item.id != -1) {
                HANDLE hFile = CreateFileW(item.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    LARGE_INTEGER fileSize;
                    GetFileSizeEx(hFile, &fileSize);

                    // Parse Range bytes=X-Y
                    long long startByte = 0;
                    long long endByte = fileSize.QuadPart - 1;
                    bool isPartial = false;

                    size_t rh = req.find("Range: bytes=");
                    if (rh != std::string::npos) {
                        size_t rn = req.find("\r\n", rh);
                        std::string rangeStr = req.substr(rh + 13, rn - (rh + 13));
                        size_t dash = rangeStr.find("-");
                        if (dash != std::string::npos) {
                            std::string startStr = rangeStr.substr(0, dash);
                            std::string endStr = rangeStr.substr(dash + 1);
                            if (!startStr.empty()) startByte = std::stoll(startStr);
                            if (!endStr.empty()) endByte = std::stoll(endStr);
                            isPartial = true;
                        }
                    }

                    if (endByte >= fileSize.QuadPart) endByte = fileSize.QuadPart - 1;
                    if (startByte > endByte) startByte = endByte;

                    long long contentLength = endByte - startByte + 1;
                    std::string mime(item.mimeType.begin(), item.mimeType.end());

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
                    send(clientSocket, headStr.c_str(), headStr.length(), 0);

                    LARGE_INTEGER movePos;
                    movePos.QuadPart = startByte;
                    SetFilePointerEx(hFile, movePos, NULL, FILE_BEGIN);

                    long long remaining = contentLength;
                    char fileBuf[65536];
                    DWORD bytesToRead, bytesReaded;

                    while (remaining > 0 && m_running) {
                        bytesToRead = (remaining > sizeof(fileBuf)) ? sizeof(fileBuf) : (DWORD)remaining;
                        if (!ReadFile(hFile, fileBuf, bytesToRead, &bytesReaded, NULL) || bytesReaded == 0) break;
                        
                        int sent = send(clientSocket, fileBuf, bytesReaded, 0);
                        if (sent == SOCKET_ERROR) break;
                        
                        remaining -= bytesReaded;
                    }

                    CloseHandle(hFile);
                    return;
                }
            }
            response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
            send(clientSocket, response.c_str(), response.length(), 0);
        }
    } else if (method == "POST") {
        if (path == "/upnp/control/content_directory") {
            size_t headersEnd = req.find("\r\n\r\n");
            std::string body;
            if (headersEnd != std::string::npos) {
                size_t bodyStart = headersEnd + 4;
                body = req.substr(bodyStart);

                // Quick Content-Length parse to read more if body was cut off
                size_t clPos = req.find("Content-Length: ");
                if (clPos != std::string::npos) {
                    size_t clEnd = req.find("\r\n", clPos);
                    int cl = std::stoi(req.substr(clPos + 16, clEnd - clPos - 16));
                    while (body.length() < cl && m_running) {
                        int r = recv(clientSocket, buf, HTTP_BUF_SIZE, 0);
                        if (r <= 0) break;
                        body.append(buf, r);
                    }
                }
                
                std::string browseResp = AppContent.HandleBrowse(body, hostUrl);
                std::string headers = "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + std::to_string(browseResp.length()) + "\r\nConnection: close\r\n\r\n";
                send(clientSocket, headers.c_str(), headers.length(), 0);
                send(clientSocket, browseResp.c_str(), browseResp.length(), 0);
            }
        }
    }
}
