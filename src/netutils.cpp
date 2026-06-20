#include "netutils.h"
#include "dlna_utils.h"
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")

namespace {
bool IsIPv4Apipa(const SOCKADDR_IN* addr) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&addr->sin_addr.S_un.S_addr);
    return bytes[0] == 169 && bytes[1] == 254;
}

bool IsUsableIPv6(const SOCKADDR_IN6* addr) {
    return !IN6_IS_ADDR_UNSPECIFIED(&addr->sin6_addr) &&
           !IN6_IS_ADDR_LOOPBACK(&addr->sin6_addr) &&
           !IN6_IS_ADDR_MULTICAST(&addr->sin6_addr);
}

int IPv6Rank(const SOCKADDR_IN6* addr) {
    if (!IsUsableIPv6(addr)) {
        return 0;
    }

    if (!IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) {
        return 2;
    }

    return 1;
}

std::string BuildEndpointHost(const SOCKADDR* addr, ULONG interfaceIndex) {
    char buffer[INET6_ADDRSTRLEN + 1] = {};

    if (addr->sa_family == AF_INET) {
        const SOCKADDR_IN* addr4 = reinterpret_cast<const SOCKADDR_IN*>(addr);
        inet_ntop(AF_INET, &addr4->sin_addr, buffer, sizeof(buffer));
        return buffer;
    }

    const SOCKADDR_IN6* addr6 = reinterpret_cast<const SOCKADDR_IN6*>(addr);
    inet_ntop(AF_INET6, &addr6->sin6_addr, buffer, sizeof(buffer));

    std::string host = "[";
    host += buffer;
    if (IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr) && interfaceIndex != 0) {
        host += "%25";
        host += std::to_string(interfaceIndex);
    }
    host += "]";
    return host;
}

bool PrefixMatchBits(const unsigned char* a, const unsigned char* b, size_t bitCount) {
    size_t fullBytes = bitCount / 8;
    size_t partialBits = bitCount % 8;

    if (fullBytes > 0 && memcmp(a, b, fullBytes) != 0) {
        return false;
    }

    if (partialBits == 0) {
        return true;
    }

    unsigned char mask = static_cast<unsigned char>(0xFF << (8 - partialBits));
    return (a[fullBytes] & mask) == (b[fullBytes] & mask);
}

bool PrefixMatch(const NetworkEndpoint& endpoint, const SOCKADDR* remoteAddr) {
    if (endpoint.family != remoteAddr->sa_family) {
        return false;
    }

    if (endpoint.family == AF_INET) {
        const SOCKADDR_IN* local4 = reinterpret_cast<const SOCKADDR_IN*>(&endpoint.sockaddr);
        const SOCKADDR_IN* remote4 = reinterpret_cast<const SOCKADDR_IN*>(remoteAddr);
        ULONG prefix = std::min<ULONG>(endpoint.prefixLength, 32);
        return PrefixMatchBits(reinterpret_cast<const unsigned char*>(&local4->sin_addr),
                               reinterpret_cast<const unsigned char*>(&remote4->sin_addr),
                               prefix);
    }

    const SOCKADDR_IN6* local6 = reinterpret_cast<const SOCKADDR_IN6*>(&endpoint.sockaddr);
    const SOCKADDR_IN6* remote6 = reinterpret_cast<const SOCKADDR_IN6*>(remoteAddr);
    ULONG prefix = std::min<ULONG>(endpoint.prefixLength, 128);
    return PrefixMatchBits(local6->sin6_addr.u.Byte, remote6->sin6_addr.u.Byte, prefix);
}

void AddEndpointForUnicast(std::vector<NetworkEndpoint>& endpoints,
                           const IP_ADAPTER_ADDRESSES* adapter,
                           const IP_ADAPTER_UNICAST_ADDRESS* unicast,
                           int port) {
    NetworkEndpoint endpoint = {};
    endpoint.family = unicast->Address.lpSockaddr->sa_family;
    endpoint.interfaceIndex = (endpoint.family == AF_INET6) ? adapter->Ipv6IfIndex : adapter->IfIndex;
    endpoint.prefixLength = unicast->OnLinkPrefixLength;
    endpoint.sockaddrLen = unicast->Address.iSockaddrLength;
    memcpy(&endpoint.sockaddr, unicast->Address.lpSockaddr, endpoint.sockaddrLen);

    if (endpoint.family == AF_INET6) {
        SOCKADDR_IN6* addr6 = reinterpret_cast<SOCKADDR_IN6*>(&endpoint.sockaddr);
        endpoint.isLinkLocal = IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr) != 0;
        if (endpoint.isLinkLocal && endpoint.interfaceIndex != 0) {
            addr6->sin6_scope_id = endpoint.interfaceIndex;
        }
    } else {
        endpoint.isLinkLocal = false;
    }

    endpoint.interfaceName = WideToUtf8(std::wstring(adapter->FriendlyName ? adapter->FriendlyName : L""));
    endpoint.address = SockaddrToLiteral(reinterpret_cast<const SOCKADDR*>(&endpoint.sockaddr));
    endpoint.host = BuildEndpointHost(reinterpret_cast<const SOCKADDR*>(&endpoint.sockaddr), endpoint.interfaceIndex);
    endpoint.locationUrl = "http://" + endpoint.host + ":" + std::to_string(port) + "/description.xml";
    endpoints.push_back(endpoint);
}
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return std::string();
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), NULL, 0, NULL, NULL);
    if (size <= 0) {
        return std::string();
    }

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], size, NULL, NULL);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return std::wstring();
    }

    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), NULL, 0);
    if (size <= 0) {
        return std::wstring();
    }

    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], size);
    return result;
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
    std::string value = ToLowerAscii(TrimAscii(ipAddress));
    if (!value.empty() && value.front() == '[' && value.back() == ']') {
        value = value.substr(1, value.size() - 2);
    }

    size_t zonePos = value.find('%');
    if (zonePos != std::string::npos) {
        value.erase(zonePos);
    }

    return value;
}

std::string SockaddrToLiteral(const SOCKADDR* addr) {
    char host[NI_MAXHOST] = {};
    DWORD flags = NI_NUMERICHOST;
    if (getnameinfo(addr,
                    (addr->sa_family == AF_INET) ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6),
                    host,
                    sizeof(host),
                    NULL,
                    0,
                    flags) == 0) {
        return host;
    }

    return std::string();
}

std::string SockaddrToHostPort(const SOCKADDR* addr, int port) {
    if (addr == NULL) {
        return std::string();
    }

    if (addr->sa_family == AF_INET6) {
        const SOCKADDR_IN6* addr6 = reinterpret_cast<const SOCKADDR_IN6*>(addr);
        std::string host = BuildEndpointHost(addr, addr6->sin6_scope_id);
        return host + ":" + std::to_string(port);
    }

    return SockaddrToLiteral(addr) + ":" + std::to_string(port);
}

std::string BuildHttpDateHeaderValue() {
    static const char* kWeekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char* kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    SYSTEMTIME st = {};
    GetSystemTime(&st);

    char buffer[64];
    sprintf_s(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT",
              kWeekdays[st.wDayOfWeek],
              st.wDay,
              kMonths[st.wMonth - 1],
              st.wYear,
              st.wHour,
              st.wMinute,
              st.wSecond);
    return buffer;
}

bool EnumerateNetworkEndpoints(int port, std::vector<NetworkEndpoint>& endpoints) {
    endpoints.clear();

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG bufferSize = 16 * 1024;
    std::vector<unsigned char> buffer(bufferSize);
    IP_ADAPTER_ADDRESSES* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    ULONG status = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addresses, &bufferSize);
    if (status == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        status = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addresses, &bufferSize);
    }

    if (status != NO_ERROR) {
        return false;
    }

    for (IP_ADAPTER_ADDRESSES* adapter = addresses; adapter; adapter = adapter->Next) {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->OperStatus != IfOperStatusUp) {
            continue;
        }
        if (adapter->Flags & IP_ADAPTER_NO_MULTICAST) {
            continue;
        }

        const IP_ADAPTER_UNICAST_ADDRESS* chosenV6 = NULL;
        int chosenV6Rank = 0;

        for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr) {
                continue;
            }

            if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                const SOCKADDR_IN* addr4 = reinterpret_cast<const SOCKADDR_IN*>(unicast->Address.lpSockaddr);
                if (!IsIPv4Apipa(addr4)) {
                    AddEndpointForUnicast(endpoints, adapter, unicast, port);
                }
            } else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
                const SOCKADDR_IN6* addr6 = reinterpret_cast<const SOCKADDR_IN6*>(unicast->Address.lpSockaddr);
                int rank = IPv6Rank(addr6);
                if (rank > chosenV6Rank) {
                    chosenV6 = unicast;
                    chosenV6Rank = rank;
                }
            }
        }

        if (chosenV6 && chosenV6->Address.lpSockaddr) {
            AddEndpointForUnicast(endpoints, adapter, chosenV6, port);
        }
    }

    return !endpoints.empty();
}

const NetworkEndpoint* SelectBestEndpoint(const std::vector<NetworkEndpoint>& endpoints, const SOCKADDR* remoteAddr) {
    if (remoteAddr == NULL) {
        return endpoints.empty() ? NULL : &endpoints.front();
    }

    const NetworkEndpoint* best = NULL;
    int bestScore = -1;

    for (const auto& endpoint : endpoints) {
        if (endpoint.family != remoteAddr->sa_family) {
            continue;
        }

        int score = 0;
        if (PrefixMatch(endpoint, remoteAddr)) {
            score += 100;
        }

        if (endpoint.family == AF_INET6) {
            const SOCKADDR_IN6* remote6 = reinterpret_cast<const SOCKADDR_IN6*>(remoteAddr);
            if (endpoint.isLinkLocal && remote6->sin6_scope_id != 0 && remote6->sin6_scope_id == endpoint.interfaceIndex) {
                score += 50;
            }
        }

        if (!endpoint.isLinkLocal) {
            score += 10;
        }

        if (score > bestScore) {
            best = &endpoint;
            bestScore = score;
        }
    }

    if (best != NULL) {
        return best;
    }

    for (const auto& endpoint : endpoints) {
        if (endpoint.family == remoteAddr->sa_family) {
            return &endpoint;
        }
    }

    return endpoints.empty() ? NULL : &endpoints.front();
}
