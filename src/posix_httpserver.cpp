#include "httpserver.h"
#include "config.h"
#include "contentdirectory.h"
#include "ipwhitelist.h"
#include "log.h"
#include "media_sources.h"
#include "netutils.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr int kBufferSize = 8192;

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

std::string TrimAscii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(start, end - start);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string HeaderValue(const std::string& request, const std::string& name) {
    const std::string needle = ToLowerAscii(name) + ":";
    size_t pos = request.find("\r\n");
    while (pos != std::string::npos) {
        pos += 2;
        const size_t end = request.find("\r\n", pos);
        if (end == std::string::npos || end == pos) break;
        const std::string line = request.substr(pos, end - pos);
        if (ToLowerAscii(line).rfind(needle, 0) == 0) return TrimAscii(line.substr(name.size() + 1));
        pos = end;
    }
    return {};
}

bool TryParseInt(const std::string& text, int& value) {
    try {
        size_t used = 0;
        int parsed = std::stoi(text, &used);
        if (used != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool TryParseLongLong(const std::string& text, long long& value) {
    try {
        size_t used = 0;
        long long parsed = std::stoll(text, &used);
        if (used != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

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

void SendAll(int fd, const std::string& bytes) {
    const char* data = bytes.data();
    size_t remaining = bytes.size();
    while (remaining > 0) {
        ssize_t sent = send(fd, data, remaining, 0);
        if (sent <= 0) return;
        data += sent;
        remaining -= static_cast<size_t>(sent);
    }
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
    if (m_listenSocketV4 >= 0) { shutdown(m_listenSocketV4, SHUT_RDWR); close(m_listenSocketV4); m_listenSocketV4 = -1; }
    if (m_listenSocketV6 >= 0) { shutdown(m_listenSocketV6, SHUT_RDWR); close(m_listenSocketV6); m_listenSocketV6 = -1; }
    for (auto& thread : m_threads) if (thread.joinable()) thread.join();
    m_threads.clear();
}

void HttpServer::AcceptLoop(int listenSocket) {
    while (m_running) {
        sockaddr_storage remote{};
        socklen_t len = sizeof(remote);
        int client = accept(listenSocket, reinterpret_cast<sockaddr*>(&remote), &len);
        if (client < 0) continue;
        std::string clientIp = NormalizeIpLiteral(SockaddrToLiteral(reinterpret_cast<sockaddr*>(&remote)));
        if (!IPWhitelist::Get().IsAllowed(clientIp)) {
            LogPrint(L"Blocked connection from %hs", clientIp.c_str());
            static const char* forbidden = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            send(client, forbidden, strlen(forbidden), 0);
            close(client);
            continue;
        }
        std::thread(&HttpServer::HandleClient, this, client, clientIp).detach();
    }
}

void HttpServer::HandleClient(int clientSocket, const std::string&) {
    ScopedFd client(clientSocket);
    char buf[kBufferSize];
    ssize_t readBytes = recv(clientSocket, buf, sizeof(buf) - 1, 0);
    if (readBytes <= 0) return;
    buf[readBytes] = '\0';
    std::string req(buf, static_cast<size_t>(readBytes));
    const size_t firstLineEnd = req.find("\r\n");
    if (firstLineEnd == std::string::npos) return;
    const std::string firstLine = req.substr(0, firstLineEnd);
    const size_t space1 = firstLine.find(' ');
    const size_t space2 = firstLine.rfind(' ');
    if (space1 == std::string::npos || space2 <= space1) return;
    const std::string method = firstLine.substr(0, space1);
    const std::string path = firstLine.substr(space1 + 1, space2 - space1 - 1);
    std::string hostUrl = HeaderValue(req, "Host");
    if (hostUrl.empty()) hostUrl = "127.0.0.1:" + std::to_string(AppConfig.port);

    auto sendText = [&](const std::string& status, const std::string& type, const std::string& body) {
        SendAll(clientSocket, "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    };

    if (method == "GET" || method == "HEAD") {
        if (path == "/description.xml") sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetDeviceDescriptionXML());
        else if (path == "/ContentDirectory.xml") sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetContentDirectoryXML());
        else if (path == "/ConnectionManager.xml") sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.GetConnectionManagerXML());
        else if (path.rfind("/media/", 0) == 0) {
            int mediaId = -1;
            if (!TryParseInt(path.substr(7), mediaId)) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            MediaItem item = AppMedia.GetItem(mediaId);
            ScopedFd fd(item.id == -1 ? -1 : open(WideToUtf8(item.path).c_str(), O_RDONLY));
            if (fd.get() < 0) {
                SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            } else {
                struct stat st{};
                if (fstat(fd.get(), &st) != 0) {
                    SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                long long start = 0;
                long long end = st.st_size - 1;
                bool partial = false;
                const std::string range = HeaderValue(req, "Range");
                if (range.rfind("bytes=", 0) == 0) {
                    const size_t dash = range.find('-', 6);
                    if (dash != std::string::npos) {
                        std::string first = TrimAscii(range.substr(6, dash - 6));
                        std::string last = TrimAscii(range.substr(dash + 1));
                        if (!first.empty() && !TryParseLongLong(first, start)) {
                            SendAll(clientSocket, "HTTP/1.1 416 Range Not Satisfiable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }
                        if (!last.empty() && !TryParseLongLong(last, end)) {
                            SendAll(clientSocket, "HTTP/1.1 416 Range Not Satisfiable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                            return;
                        }
                        partial = true;
                    }
                }
                if (start < 0 || end < 0) {
                    SendAll(clientSocket, "HTTP/1.1 416 Range Not Satisfiable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                    return;
                }
                if (end >= st.st_size) end = st.st_size - 1;
                if (start > end) start = end;
                std::stringstream headers;
                headers << "HTTP/1.1 " << (partial ? "206 Partial Content" : "200 OK") << "\r\n";
                if (partial) headers << "Content-Range: bytes " << start << "-" << end << "/" << st.st_size << "\r\n";
                headers << "Content-Type: " << WideToUtf8(item.mimeType) << "\r\n"
                        << "Content-Length: " << (end - start + 1) << "\r\n"
                        << "Accept-Ranges: bytes\r\nConnection: close\r\n"
                        << "transferMode.dlna.org: Streaming\r\n"
                        << "contentFeatures.dlna.org: DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01700000000000000000000000000000\r\n\r\n";
                SendAll(clientSocket, headers.str());
                if (method == "GET") {
                    lseek(fd.get(), start, SEEK_SET);
                    long long remaining = end - start + 1;
                    while (remaining > 0) {
                        ssize_t chunk = read(fd.get(), buf, std::min<long long>(sizeof(buf), remaining));
                        if (chunk <= 0) break;
                        if (send(clientSocket, buf, chunk, 0) <= 0) break;
                        remaining -= chunk;
                    }
                }
            }
        } else {
            SendAll(clientSocket, "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        }
    } else if (method == "POST" && path == "/upnp/control/content_directory") {
        const size_t headersEnd = req.find("\r\n\r\n");
        std::string body = headersEnd == std::string::npos ? std::string() : req.substr(headersEnd + 4);
        const std::string lengthText = HeaderValue(req, "Content-Length");
        if (!lengthText.empty()) {
            int contentLength = 0;
            if (!TryParseInt(lengthText, contentLength) || contentLength < 0) {
                SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                return;
            }
            while (static_cast<int>(body.size()) < contentLength) {
                ssize_t r = recv(clientSocket, buf, sizeof(buf), 0);
                if (r <= 0) break;
                body.append(buf, static_cast<size_t>(r));
            }
        }
        sendText("200 OK", "text/xml; charset=\"utf-8\"", AppContent.HandleBrowse(body, hostUrl));
    } else {
        SendAll(clientSocket, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
    }
}
