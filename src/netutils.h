#ifndef NETUTILS_H
#define NETUTILS_H

#include <string>
#include <vector>
#include <winsock2.h>

struct NetworkEndpoint {
    int family;
    ULONG interfaceIndex;
    ULONG prefixLength;
    bool isLinkLocal;
    std::string address;
    std::string host;
    std::string locationUrl;
    SOCKADDR_STORAGE sockaddr;
    int sockaddrLen;
};

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::string XMLEscapeUtf8(const std::string& value);
std::string NormalizeIpLiteral(const std::string& ipAddress);
std::string SockaddrToLiteral(const SOCKADDR* addr);
std::string SockaddrToHostPort(const SOCKADDR* addr, int port);
std::string BuildHttpDateHeaderValue();

bool EnumerateNetworkEndpoints(int port, std::vector<NetworkEndpoint>& endpoints);
const NetworkEndpoint* SelectBestEndpoint(const std::vector<NetworkEndpoint>& endpoints, const SOCKADDR* remoteAddr);

#endif // NETUTILS_H
