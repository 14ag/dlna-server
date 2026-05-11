#include "netutils.h"

#include <arpa/inet.h>
#include <cstring>
#include <ctime>
#include <ifaddrs.h>
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

bool EnumerateNetworkEndpoints(int port, std::vector<NetworkEndpoint>& endpoints) {
    endpoints.clear();
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return false;
    for (ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr) continue;
        if (!(it->ifa_flags & IFF_UP) || !(it->ifa_flags & IFF_MULTICAST) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        if (it->ifa_addr->sa_family != AF_INET && it->ifa_addr->sa_family != AF_INET6) continue;

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
            endpoint.isLinkLocal = false;
            endpoint.host = SockaddrToLiteral(reinterpret_cast<SOCKADDR*>(&endpoint.sockaddr));
        }
        endpoint.address = SockaddrToLiteral(reinterpret_cast<SOCKADDR*>(&endpoint.sockaddr));
        endpoint.locationUrl = "http://" + endpoint.host + ":" + std::to_string(port) + "/description.xml";
        endpoints.push_back(endpoint);
    }
    freeifaddrs(list);
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
            if (PrefixMatchBits(local->sin6_addr.s6_addr, remote->sin6_addr.s6_addr, endpoint.prefixLength)) score += 100;
        }
        if (score > bestScore) {
            best = &endpoint;
            bestScore = score;
        }
    }
    return best ? best : (endpoints.empty() ? nullptr : &endpoints.front());
}
