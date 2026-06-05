#include "ipwhitelist.h"
#include "dlna_utils.h"
#include "netutils.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>
#include <shared_mutex>

IPWhitelist& IPWhitelist::Get() {
    static IPWhitelist instance;
    return instance;
}

IPWhitelist::IPWhitelist() {
}

bool ParseAddressBytes(const std::string& text, int& family, std::array<unsigned char, 16>& bytes) {
    bytes.fill(0);
    in_addr addr4{};
    if (inet_pton(AF_INET, text.c_str(), &addr4) == 1) {
        family = AF_INET;
        std::memcpy(bytes.data(), &addr4, 4);
        return true;
    }
    in6_addr addr6{};
    if (inet_pton(AF_INET6, text.c_str(), &addr6) == 1) {
        family = AF_INET6;
        std::memcpy(bytes.data(), &addr6, 16);
        return true;
    }
    return false;
}

bool ParseCidrRange(const std::string& token, CidrRange& range) {
    const size_t slash = token.find('/');
    if (slash == std::string::npos) return false;
    std::string addressText = NormalizeIpLiteral(TrimAscii(token.substr(0, slash)));
    std::string prefixText = TrimAscii(token.substr(slash + 1));
    int prefix = 0;
    int family = AF_UNSPEC;
    std::array<unsigned char, 16> bytes{};
    if (!TryParseIntStrict(prefixText, prefix) || !ParseAddressBytes(addressText, family, bytes)) return false;
    int maxPrefix = family == AF_INET ? 32 : 128;
    if (prefix < 0 || prefix > maxPrefix) return false;
    range.family = family;
    range.address = bytes;
    range.prefixLength = prefix;
    return true;
}

bool PrefixBytesMatch(const unsigned char* left, const unsigned char* right, int bitCount) {
    int fullBytes = bitCount / 8;
    int partialBits = bitCount % 8;
    if (fullBytes > 0 && std::memcmp(left, right, static_cast<size_t>(fullBytes)) != 0) return false;
    if (partialBits == 0) return true;
    unsigned char mask = static_cast<unsigned char>(0xff << (8 - partialBits));
    return (left[fullBytes] & mask) == (right[fullBytes] & mask);
}

bool IsInRange(const CidrRange& range, const std::string& ipAddress) {
    int family = AF_UNSPEC;
    std::array<unsigned char, 16> bytes{};
    if (!ParseAddressBytes(ipAddress, family, bytes) || family != range.family) return false;
    return PrefixBytesMatch(range.address.data(), bytes.data(), range.prefixLength);
}

void IPWhitelist::Load(const std::wstring& configStr) {
    std::unordered_set<std::string> parsed;
    std::vector<CidrRange> ranges;

    if (!configStr.empty()) {
        size_t start = 0;
        size_t end = configStr.find(L',');
        while (end != std::wstring::npos) {
            std::wstring tk = configStr.substr(start, end - start);
            std::string ipTk = NormalizeIpLiteral(TrimAscii(WideToUtf8(tk)));
            CidrRange range{};
            if (ParseCidrRange(ipTk, range)) ranges.push_back(range);
            else if (!ipTk.empty()) parsed.insert(ipTk);
            start = end + 1;
            end = configStr.find(L',', start);
        }
        std::wstring tk = configStr.substr(start);
        if (!tk.empty()) {
            std::string ipTk = NormalizeIpLiteral(TrimAscii(WideToUtf8(tk)));
            CidrRange range{};
            if (ParseCidrRange(ipTk, range)) ranges.push_back(range);
            else if (!ipTk.empty()) parsed.insert(ipTk);
        }
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_allowedIps.clear();
    m_allowedRanges.clear();
    m_allowedIps.swap(parsed);
    m_allowedRanges.swap(ranges);
}

bool IPWhitelist::IsAllowed(const std::string& ipAddress) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (m_allowedIps.empty() && m_allowedRanges.empty()) return true; // Empty means all allowed

    std::string normalized = NormalizeIpLiteral(TrimAscii(ipAddress));

    if (m_allowedIps.find(normalized) != m_allowedIps.end()) return true;
    for (const auto& range : m_allowedRanges) if (IsInRange(range, normalized)) return true;
    return false;
}
