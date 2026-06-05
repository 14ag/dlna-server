#ifndef IPWHITELIST_H
#define IPWHITELIST_H

#include <string>
#include <array>
#include <vector>
#include <shared_mutex>
#include <unordered_set>

struct CidrRange {
    int family;
    std::array<unsigned char, 16> address;
    int prefixLength;
};

class IPWhitelist {
public:
    static IPWhitelist& Get();

    void Load(const std::wstring& configStr);
    bool IsAllowed(const std::string& ipAddress);

private:
    IPWhitelist();
    mutable std::shared_mutex m_mutex;
    std::unordered_set<std::string> m_allowedIps;
    std::vector<CidrRange> m_allowedRanges;
};

#endif // IPWHITELIST_H
