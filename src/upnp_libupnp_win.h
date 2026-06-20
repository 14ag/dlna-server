#ifndef UPNP_LIBUPNP_WIN_H
#define UPNP_LIBUPNP_WIN_H

#ifdef _WIN32

#include "netutils.h"
#include <string>
#include <vector>

class LibUPnPWrapper {
public:
    static LibUPnPWrapper& Get();

    bool Start(const std::vector<NetworkEndpoint>& endpoints, int httpPort, const std::wstring& serverName, const std::wstring& deviceUUID);
    void Stop();

    std::string GetHttpAddr() const;
    void NotifyUpdateId(int updateId);

private:
    LibUPnPWrapper() {}
    ~LibUPnPWrapper() { Stop(); }

    std::string m_tempDir;
    std::string m_httpAddr;
    int m_deviceHandle = -1;
};

#endif // _WIN32
#endif // UPNP_LIBUPNP_WIN_H
