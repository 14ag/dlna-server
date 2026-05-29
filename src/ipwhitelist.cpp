#include "ipwhitelist.h"
#include "netutils.h"

#include <mutex>
#include <shared_mutex>

IPWhitelist& IPWhitelist::Get() {
    static IPWhitelist instance;
    return instance;
}

IPWhitelist::IPWhitelist() {
}

void IPWhitelist::Load(const std::wstring& configStr) {
    std::vector<std::string> parsed;

    if (!configStr.empty()) {
        size_t start = 0;
        size_t end = configStr.find(L',');
        while (end != std::wstring::npos) {
            std::wstring tk = configStr.substr(start, end - start);
            std::string ipTk = NormalizeIpLiteral(WideToUtf8(tk));
            if (!ipTk.empty()) parsed.push_back(ipTk);
            start = end + 1;
            end = configStr.find(L',', start);
        }
        std::wstring tk = configStr.substr(start);
        if (!tk.empty()) {
            std::string ipTk = NormalizeIpLiteral(WideToUtf8(tk));
            if (!ipTk.empty()) parsed.push_back(ipTk);
        }
    }

    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_allowedIps.clear();
    m_allowedIps.swap(parsed);
}

bool IPWhitelist::IsAllowed(const std::string& ipAddress) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if (m_allowedIps.empty()) return true; // Empty means all allowed

    std::string normalized = NormalizeIpLiteral(ipAddress);

    for (const auto& allowed : m_allowedIps) {
        if (normalized == allowed) return true;
    }
    return false;
}
