#ifndef IPWHITELIST_H
#define IPWHITELIST_H

#include <string>
#include <vector>
#include <shared_mutex>

class IPWhitelist {
public:
    static IPWhitelist& Get();

    void Load(const std::wstring& configStr);
    bool IsAllowed(const std::string& ipAddress);

private:
    IPWhitelist();
    mutable std::shared_mutex m_mutex;
    std::vector<std::string> m_allowedIps;
};

#endif // IPWHITELIST_H
