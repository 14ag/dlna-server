#include "ipwhitelist.h"

IPWhitelist& IPWhitelist::Get() {
    static IPWhitelist instance;
    return instance;
}

IPWhitelist::IPWhitelist() {
}

void IPWhitelist::Load(const std::wstring& configStr) {
    m_allowedIps.clear();
    
    if (configStr.empty()) return;

    size_t start = 0;
    size_t end = configStr.find(L',');
    while (end != std::wstring::npos) {
        std::wstring tk = configStr.substr(start, end - start);
        std::string ipTk(tk.begin(), tk.end());
        if(!ipTk.empty()) m_allowedIps.push_back(ipTk);
        start = end + 1;
        end = configStr.find(L',', start);
    }
    std::wstring tk = configStr.substr(start);
    if (!tk.empty()) {
        std::string ipTk(tk.begin(), tk.end());
        m_allowedIps.push_back(ipTk);
    }
}

bool IPWhitelist::IsAllowed(const std::string& ipAddress) {
    if (m_allowedIps.empty()) return true; // Empty means all allowed

    for (const auto& allowed : m_allowedIps) {
        if (ipAddress == allowed) return true;
    }
    return false;
}
