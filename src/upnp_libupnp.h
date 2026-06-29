#ifndef UPNP_LIBUPNP_H
#define UPNP_LIBUPNP_H

#include <upnp/upnp.h>
#include "netutils.h"
#include <string>
#include <vector>

class LibUPnPWrapper {
public:
    static LibUPnPWrapper& Get();

    bool Start(const std::vector<NetworkEndpoint>& endpoints, int httpPort, const std::wstring& serverName, const std::wstring& deviceUUID);
    void Stop();

    std::string GetHttpAddr() const;
    UpnpDevice_Handle GetDeviceHandle() const { return m_deviceHandle; }
    void NotifyUpdateId(int updateId);

private:
    LibUPnPWrapper() {}
    ~LibUPnPWrapper() { Stop(); }

    std::string m_httpAddr;
    UpnpDevice_Handle m_deviceHandle = -1;
};

#endif // UPNP_LIBUPNP_H
