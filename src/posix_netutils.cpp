#include "netutils.h"
#include "network_interface_policy.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <netdb.h>
#include <unistd.h>

namespace {
bool PrefixMatchBits(const unsigned char* a, const unsigned char* b, size_t bitCount) {
    const size_t fullBytes = bitCount / 8;
    const size_t partialBits = bitCount % 8;
    if (fullBytes > 0 && std::memcmp(a, b, fullBytes) != 0) return false;
    if (partialBits == 0) return true;
    const unsigned char mask = static_cast<unsigned char>(0xff << (8 - partialBits));
    return (a[fullBytes] & mask) == (b[fullBytes] & mask);
}

ULONG PrefixLengthFromNetmask(const sockaddr* netmask) {
    if (!netmask) return 0;
    ULONG bits = 0;
    if (netmask->sa_family == AF_INET) {
        auto* addr = reinterpret_cast<const sockaddr_in*>(netmask);
        uint32_t mask = ntohl(addr->sin_addr.s_addr);
        while (mask & 0x80000000u) {
            ++bits;
            mask <<= 1;
        }
    } else if (netmask->sa_family == AF_INET6) {
        auto* addr = reinterpret_cast<const sockaddr_in6*>(netmask);
        for (unsigned char byte : addr->sin6_addr.s6_addr) {
            for (int i = 7; i >= 0; --i) {
                if (byte & (1u << i)) ++bits;
                else return bits;
            }
        }
    }
    return bits;
}

bool IsIPv4Apipa(const sockaddr_in* addr) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&addr->sin_addr.s_addr);
    return bytes[0] == 169 && bytes[1] == 254;
}

const char* const kVirtualAdapterNamePrefixes[] = {
    "docker", "veth", "vboxnet", "vmnet", "tailscale", "zt", "wg", "tun", "tap", "br-",
};

bool IsLikelyVirtualAdapterName(const char* ifaceName) {
    for (const char* prefix : kVirtualAdapterNamePrefixes) {
        if (std::strncmp(ifaceName, prefix, std::strlen(prefix)) == 0) return true;
    }
    return false;
}

// Returns the local IPv4/IPv6 literal address the kernel would choose
// as the source address for a UDP packet sent to a well-known public
// destination, or an empty string if no route currently exists for
// that family (offline host, isolated network namespace, sandboxed CI
// runner, etc.). No packet is transmitted: UDP connect() only performs
// a routing-table lookup and binds the socket's local endpoint. See
// dlna-server-network-endpoint-gateway-fix-workflow-30-7-26.md, Task 3,
// citations C5/C6. Call once per family per EnumerateNetworkEndpoints
// invocation and reuse the result for every candidate adapter -- do
// not call this inside the per-adapter loop.
std::string DetectDefaultRouteSourceAddress(int family) {
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) return {};

    bool connected = false;
    if (family == AF_INET) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);
        connected = connect(fd, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == 0;
    } else if (family == AF_INET6) {
        sockaddr_in6 dest{};
        dest.sin6_family = AF_INET6;
        dest.sin6_port = htons(53);
        inet_pton(AF_INET6, "2001:4860:4860::8888", &dest.sin6_addr);
        connected = connect(fd, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) == 0;
    }

    std::string result;
    if (connected) {
        sockaddr_storage local{};
        socklen_t localLen = sizeof(local);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &localLen) == 0) {
            result = SockaddrToLiteral(reinterpret_cast<SOCKADDR*>(&local));
        }
    }
    close(fd);
    return result;
}

bool InterfaceAllowListPermits(const std::wstring& allowList, const char* ifaceName, bool hasDefaultGateway) {
    if (allowList.empty()) {
        return ShouldUseUnlistedInterface(IsLikelyVirtualAdapterName(ifaceName), hasDefaultGateway);
    }
    std::string name(ifaceName);
    size_t start = 0;
    while (start <= allowList.size()) {
        size_t comma = allowList.find(L',', start);
        std::wstring token = allowList.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        if (!token.empty()) {
            std::string tokenUtf8 = WideToUtf8(token);
            if (name.find(tokenUtf8) != std::string::npos) return true;
        }
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return false;
}
}

std::string WideToUtf8(const std::wstring& value) {
    std::string out;
    for (wchar_t wide : value) {
        uint32_t cp = static_cast<uint32_t>(wide);
        if (cp <= 0x7f) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    std::wstring out;
    for (size_t i = 0; i < value.size();) {
        unsigned char ch = static_cast<unsigned char>(value[i]);
        uint32_t cp = 0xfffd;
        size_t extra = 0;
        if (ch <= 0x7f) {
            cp = ch;
        } else if ((ch & 0xe0) == 0xc0) {
            cp = ch & 0x1f;
            extra = 1;
        } else if ((ch & 0xf0) == 0xe0) {
            cp = ch & 0x0f;
            extra = 2;
        } else if ((ch & 0xf8) == 0xf0) {
            cp = ch & 0x07;
            extra = 3;
        }
        if (i + extra >= value.size()) {
            out.push_back(static_cast<wchar_t>(0xfffd));
            break;
        }
        bool valid = true;
        for (size_t j = 1; j <= extra; ++j) {
            unsigned char next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3f);
        }
        out.push_back(static_cast<wchar_t>(valid ? cp : 0xfffd));
        i += extra + 1;
    }
    return out;
}

std::string XMLEscapeUtf8(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += ch; break;
        }
    }
    return result;
}

std::string NormalizeIpLiteral(const std::string& ipAddress) {
    std::string value = ipAddress;
    if (!value.empty() && value.front() == '[' && value.back() == ']') {
        value = value.substr(1, value.size() - 2);
    }
    const size_t zone = value.find('%');
    if (zone != std::string::npos) value.erase(zone);
    return value;
}

std::string SockaddrToLiteral(const SOCKADDR* addr) {
    char host[NI_MAXHOST] = {};
    if (getnameinfo(addr,
                    addr->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
                    host,
                    sizeof(host),
                    nullptr,
                    0,
                    NI_NUMERICHOST) == 0) {
        return host;
    }
    return {};
}

std::string SockaddrToHostPort(const SOCKADDR* addr, int port) {
    if (addr->sa_family == AF_INET6) return "[" + SockaddrToLiteral(addr) + "]:" + std::to_string(port);
    return SockaddrToLiteral(addr) + ":" + std::to_string(port);
}

std::string BuildHttpDateHeaderValue() {
    char buffer[64];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return buffer;
}

bool EnumerateNetworkEndpoints(int port, const std::wstring& interfaceAllowList, std::vector<NetworkEndpoint>& endpoints) {
    endpoints.clear();
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return false;

    // Computed once per call, reused for every candidate adapter below.
    // See Step 3.2 for why this must not move inside the loop.
    const std::string defaultRouteV4 = DetectDefaultRouteSourceAddress(AF_INET);
    const std::string defaultRouteV6 = DetectDefaultRouteSourceAddress(AF_INET6);

    for (ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr) continue;
        if (!(it->ifa_flags & IFF_UP) || !(it->ifa_flags & IFF_MULTICAST) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        if (it->ifa_addr->sa_family != AF_INET && it->ifa_addr->sa_family != AF_INET6) continue;

        const std::string candidateAddress = SockaddrToLiteral(it->ifa_addr);
        const std::string& defaultRouteForFamily =
            (it->ifa_addr->sa_family == AF_INET) ? defaultRouteV4 : defaultRouteV6;
        const bool hasDefaultGateway = !defaultRouteForFamily.empty() && !candidateAddress.empty() &&
            NormalizeIpLiteral(defaultRouteForFamily) == NormalizeIpLiteral(candidateAddress);

        if (!InterfaceAllowListPermits(interfaceAllowList, it->ifa_name, hasDefaultGateway)) continue;

        NetworkEndpoint endpoint{};
        endpoint.family = it->ifa_addr->sa_family;
        endpoint.interfaceIndex = if_nametoindex(it->ifa_name);
        endpoint.prefixLength = PrefixLengthFromNetmask(it->ifa_netmask);
        endpoint.sockaddrLen = endpoint.family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        std::memcpy(&endpoint.sockaddr, it->ifa_addr, endpoint.sockaddrLen);
        if (endpoint.family == AF_INET6) {
            auto* addr6 = reinterpret_cast<sockaddr_in6*>(&endpoint.sockaddr);
            endpoint.isLinkLocal = IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr);
            if (endpoint.isLinkLocal) addr6->sin6_scope_id = endpoint.interfaceIndex;
            endpoint.host = "[" + SockaddrToLiteral(reinterpret_cast<SOCKADDR*>(&endpoint.sockaddr)) + "]";
        } else {
            auto* addr4 = reinterpret_cast<sockaddr_in*>(&endpoint.sockaddr);
            if (IsIPv4Apipa(addr4)) continue;
            endpoint.isLinkLocal = false;
            endpoint.host = SockaddrToLiteral(reinterpret_cast<SOCKADDR*>(&endpoint.sockaddr));
        }
        endpoint.address = SockaddrToLiteral(reinterpret_cast<SOCKADDR*>(&endpoint.sockaddr));
        endpoint.locationUrl = "http://" + endpoint.host + ":" + std::to_string(port) + "/description.xml";
        endpoints.push_back(endpoint);
    }
    freeifaddrs(list);

    // drop link local endpoints whenever something better exists see
    // ShouldDropLinkLocalEndpoint in netutils h for the full rationale
    // and the same interaction note about ssdp multicast membership
    // that the windows side of this fix documents
    const bool anyNonLinkLocalExists = std::any_of(endpoints.begin(), endpoints.end(),
        [](const NetworkEndpoint& ep) { return !ep.isLinkLocal; });
    endpoints.erase(
        std::remove_if(endpoints.begin(), endpoints.end(),
            [anyNonLinkLocalExists](const NetworkEndpoint& ep) {
                return ShouldDropLinkLocalEndpoint(ep.isLinkLocal, anyNonLinkLocalExists);
            }),
        endpoints.end());

    return !endpoints.empty();
}

const NetworkEndpoint* SelectBestEndpoint(const std::vector<NetworkEndpoint>& endpoints, const SOCKADDR* remoteAddr) {
    const NetworkEndpoint* best = nullptr;
    int bestScore = -1;
    for (const auto& endpoint : endpoints) {
        if (remoteAddr && endpoint.family != remoteAddr->sa_family) continue;
        int score = endpoint.isLinkLocal ? 0 : 10;
        if (remoteAddr && endpoint.family == AF_INET) {
            auto* local = reinterpret_cast<const sockaddr_in*>(&endpoint.sockaddr);
            auto* remote = reinterpret_cast<const sockaddr_in*>(remoteAddr);
            if (PrefixMatchBits(reinterpret_cast<const unsigned char*>(&local->sin_addr),
                                reinterpret_cast<const unsigned char*>(&remote->sin_addr),
                                endpoint.prefixLength)) score += 100;
        } else if (remoteAddr && endpoint.family == AF_INET6) {
            auto* local = reinterpret_cast<const sockaddr_in6*>(&endpoint.sockaddr);
            auto* remote = reinterpret_cast<const sockaddr_in6*>(remoteAddr);
            if (PrefixMatchBits(local->sin6_addr.s6_addr, remote->sin6_addr.s6_addr, endpoint.prefixLength)) {
                score += 100;
            }
            // a link local ipv6 prefix fe80 colon colon slash 10 rfc 4291
            // section 2 5 6 is identical on every interface of this host
            // so the prefix match above alone cannot tell which physical
            // interface a link local remote address belongs to
            // rfc 4007 ipv6 scoped address architecture section 5
            // requires the zone scope id to disambiguate a link local
            // address without this check every link local endpoint on a
            // multi homed host scores identically and whichever one is
            // first in the vector always wins even if it is on the wrong
            // interface see F-DISCOVERY-01 mirrors the same scope id
            // check netutils cpp SelectBestEndpoint already performs on
            // windows
            if (endpoint.isLinkLocal && remote->sin6_scope_id != 0 &&
                remote->sin6_scope_id == endpoint.interfaceIndex) {
                score += 50;
            }
        }
        if (score > bestScore) {
            best = &endpoint;
            bestScore = score;
        }
    }
    if (best != nullptr) return best;
    // fall back to any endpoint of the same address family before ever
    // returning an endpoint of the wrong family the previous fallback
    // ignored family entirely which the caller happened to catch with
    // its own separate family check but that made this function's
    // return value misleading on its own see F-DISCOVERY-01
    for (const auto& endpoint : endpoints) {
        if (endpoint.family == remoteAddr->sa_family) return &endpoint;
    }
    return endpoints.empty() ? nullptr : &endpoints.front();
}

bool NetworkEndpointSetsEqual(const std::vector<NetworkEndpoint>& a,
                              const std::vector<NetworkEndpoint>& b) {
    if (a.size() != b.size()) return false;
    auto keyOf = [](const NetworkEndpoint& endpoint) {
        return std::to_string(endpoint.family) + "|" + endpoint.address +
               "|" + std::to_string(endpoint.interfaceIndex);
    };
    std::vector<std::string> keysA;
    std::vector<std::string> keysB;
    keysA.reserve(a.size());
    keysB.reserve(b.size());
    for (const auto& endpoint : a) keysA.push_back(keyOf(endpoint));
    for (const auto& endpoint : b) keysB.push_back(keyOf(endpoint));
    std::sort(keysA.begin(), keysA.end());
    std::sort(keysB.begin(), keysB.end());
    return keysA == keysB;
}

bool WriteFileAtomicUtf8(const std::wstring& path, const std::string& utf8Content) {
    const std::wstring tempPath = path + L".tmp";
    FILE* fp = std::fopen(WideToUtf8(tempPath).c_str(), "wb");
    if (!fp) return false;
    const bool wroteOk = std::fwrite(utf8Content.data(), 1, utf8Content.size(), fp) == utf8Content.size();
    std::fclose(fp);
    if (!wroteOk) {
        std::remove(WideToUtf8(tempPath).c_str());
        return false;
    }
    if (std::rename(WideToUtf8(tempPath).c_str(), WideToUtf8(path).c_str()) == 0) {
        return true;
    }
    std::remove(WideToUtf8(tempPath).c_str());
    return false;
}

namespace {
// guarded because HandleClient calls GetRoutableHostUrl from many
// concurrent client threads on both platforms see the workflow
// document for the full rationale
std::mutex g_routableHostCacheMutex;
std::string g_routableHostCached;
int g_routableHostCachedPort = 0;
bool g_routableHostCacheValid = false;
long g_routableHostRecomputeCount = 0;
}

void InvalidateRoutableHostUrlCache() {
    std::lock_guard<std::mutex> lock(g_routableHostCacheMutex);
    g_routableHostCacheValid = false;
}

long GetRoutableHostUrlRecomputeCountForTest() {
    std::lock_guard<std::mutex> lock(g_routableHostCacheMutex);
    return g_routableHostRecomputeCount;
}

std::string GetRoutableHostUrl(int port, const std::wstring& interfaceAllowList) {
    std::lock_guard<std::mutex> lock(g_routableHostCacheMutex);
    if (!g_routableHostCacheValid || g_routableHostCachedPort != port) {
        ++g_routableHostRecomputeCount;
        g_routableHostCached.clear();
        std::vector<NetworkEndpoint> endpoints;
        if (EnumerateNetworkEndpoints(port, interfaceAllowList, endpoints)) {
            for (const auto& ep : endpoints) {
                if (!ep.isLinkLocal) {
                    g_routableHostCached = ep.address + ":" + std::to_string(port);
                    break;
                }
            }
        }
        g_routableHostCachedPort = port;
        g_routableHostCacheValid = true;
    }
    return g_routableHostCached;
}
