#ifndef IPWHITELIST_H
#define IPWHITELIST_H

#include <string>
#include <vector>

class IPWhitelist {
public:
    static IPWhitelist& Get();

    void Load(const std::wstring& configStr);
    bool IsAllowed(const std::string& ipAddress);

private:
    IPWhitelist();
    std::vector<std::string> m_allowedIps;
};

#endif // IPWHITELIST_H
