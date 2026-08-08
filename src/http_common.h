#ifndef HTTP_COMMON_H
#define HTTP_COMMON_H

#include <functional>
#include <string>

// 1 MiB cap on a single SOAP request body, shared by every
// ContentDirectory/ConnectionManager POST handler on both platforms.
constexpr size_t kMaxSoapBodyBytes = 1024 * 1024;
constexpr size_t kHttpBufSize = 8192;

std::string ConnectionHeader(bool keepAlive);
bool ShouldKeepAlive(const std::string& req);
std::string SplitRequestTarget(const std::string& requestTarget);
bool ValidateHostHeader(const std::string& host);

// recvOnce must behave like POSIX recv()/Winsock recv(): read up to the
// requested byte count into the buffer, return the byte count read, 0 on
// orderly shutdown, or a negative value on error. Callers supply a lambda
// closing over their platform socket handle so this file stays
// socket-API-agnostic.
bool ReadHttpRequestHeaders(const std::function<int(char*, int)>& recvOnce, std::string& req);

#endif // HTTP_COMMON_H