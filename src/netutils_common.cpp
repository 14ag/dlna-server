#include "netutils.h"
#include "dlna_utils.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

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

namespace {
// guarded because HandleClient calls GetRoutableHostUrl from many
// concurrent client threads on both platforms
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
    {
        std::lock_guard<std::mutex> lock(g_routableHostCacheMutex);
        if (g_routableHostCacheValid && g_routableHostCachedPort == port) {
            return g_routableHostCached;
        }
    }
    std::string computed;
    std::vector<NetworkEndpoint> endpoints;
    if (EnumerateNetworkEndpoints(port, interfaceAllowList, endpoints)) {
        for (const auto& ep : endpoints) {
            if (!ep.isLinkLocal) { computed = ep.address + ":" + std::to_string(port); break; }
        }
    }
    std::lock_guard<std::mutex> lock(g_routableHostCacheMutex);
    ++g_routableHostRecomputeCount;
    g_routableHostCached = computed;
    g_routableHostCachedPort = port;
    g_routableHostCacheValid = true;
    return g_routableHostCached;
}
