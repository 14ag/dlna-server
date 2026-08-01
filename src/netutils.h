#ifndef NETUTILS_H
#define NETUTILS_H

#include <string>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
using ULONG = unsigned long;
using SOCKADDR = sockaddr;
using SOCKADDR_STORAGE = sockaddr_storage;
#endif

struct NetworkEndpoint {
    int family;
    ULONG interfaceIndex;
    ULONG prefixLength;
    bool isLinkLocal;
    std::string interfaceName;
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

bool WriteFileAtomicUtf8(const std::wstring& path, const std::string& utf8Content);

bool EnumerateNetworkEndpoints(int port, const std::wstring& interfaceAllowList, std::vector<NetworkEndpoint>& endpoints);
const NetworkEndpoint* SelectBestEndpoint(const std::vector<NetworkEndpoint>& endpoints, const SOCKADDR* remoteAddr);
// pure predicate see the workflow document for the RFC 4007 citation
// a link-local ipv6 address never carries a usable zone id once it
// leaves this node per RFC 4007 section 6 the zone index is strictly
// local to the node that generated it so it cannot be forwarded to
// another node and this codebase already strips it before advertising
// any url see BuildEndpointHost in this file a bare link-local address
// left over after that stripping is commonly unusable by any client
// with more than one active network interface for example any phone
// with both wifi and cellular active drop a link-local candidate
// whenever at least one non link-local endpoint exists anywhere in
// the same enumeration pass otherwise keep it since it is all this
// machine has to offer
inline bool ShouldDropLinkLocalEndpoint(bool candidateIsLinkLocal, bool anyNonLinkLocalExistsInFullList) {
    return candidateIsLinkLocal && anyNonLinkLocalExistsInFullList;
}
// pure predicate returns true when an os-reported sockaddr length fits
// inside the fixed destination capacity and is positive the caller must
// skip the candidate address when this returns false rather than perform
// a copy that would overflow the destination see AddEndpointForUnicast
// in netutils.cpp for the CERT MEM35-C citation trail behind this check
inline bool IsSockaddrLengthSafeToCopy(int reportedLength, size_t destinationCapacity) {
    return reportedLength > 0 && static_cast<size_t>(reportedLength) <= destinationCapacity;
}
std::string GetRoutableHostUrl(int port, const std::wstring& interfaceAllowList);
// clears the cached routable host so the next call recomputes it call
// this whenever network topology changes are detected see
// Server StartNetworkChangeWatcher for the call site
void InvalidateRoutableHostUrlCache();
// test only returns how many times the cache actually recomputed
// instead of returning a cached value since process start
long GetRoutableHostUrlRecomputeCountForTest();

#endif // NETUTILS_H
