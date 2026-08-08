#include "http_common.h"
#include "dlna_utils.h"

std::string ConnectionHeader(bool keepAlive) {
    return keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
}

bool ShouldKeepAlive(const std::string& req) {
    std::string connectionValue = ToLowerAscii(FindHeaderValueCaseInsensitive(req, "Connection"));
    if (connectionValue == "close") return false;
    if (connectionValue == "keep-alive") return true;
    // HTTP/1.1 defaults to keep-alive unless the client says close.
    // HTTP/1.0 defaults to close unless the client says keep-alive.
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

bool ReadHttpRequestHeaders(const std::function<int(char*, int)>& recvOnce, std::string& req) {
    req.clear();
    char buffer[kHttpBufSize];
    constexpr size_t kMaxHeaderBytes = 64 * 1024;
    while (req.find("\r\n\r\n") == std::string::npos) {
        int bytesRead = recvOnce(buffer, static_cast<int>(sizeof(buffer)));
        if (bytesRead <= 0) return false;
        req.append(buffer, static_cast<size_t>(bytesRead));
        if (req.size() > kMaxHeaderBytes) return false;
    }
    return true;
}