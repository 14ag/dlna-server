#include "upnp_libupnp_win.h"

#ifdef _WIN32

#include "config.h"
#include "contentdirectory.h"
#include "dlna_utils.h"
#include "log.h"

#include <upnp/upnp.h>

namespace {
int LibUpnpCallback(Upnp_EventType, void*, void*) {
    return 0;
}

const NetworkEndpoint* PickBestEndpoint(const std::vector<NetworkEndpoint>& endpoints) {
    const NetworkEndpoint* best = nullptr;
    ULONG bestPrefix = 0;
    for (const auto& endpoint : endpoints) {
        if (endpoint.family != AF_INET || endpoint.host.empty() || endpoint.isLinkLocal) {
            continue;
        }
        if (best == nullptr || endpoint.prefixLength > bestPrefix) {
            best = &endpoint;
            bestPrefix = endpoint.prefixLength;
        }
    }
    if (best != nullptr) {
        return best;
    }
    for (const auto& endpoint : endpoints) {
        if (endpoint.family == AF_INET && !endpoint.host.empty()) {
            return &endpoint;
        }
    }
    for (const auto& endpoint : endpoints) {
        if (!endpoint.host.empty()) {
            return &endpoint;
        }
    }
    return nullptr;
}
} // namespace

LibUPnPWrapper& LibUPnPWrapper::Get() {
    static LibUPnPWrapper instance;
    return instance;
}

bool LibUPnPWrapper::Start(const std::vector<NetworkEndpoint>& endpoints,
                           int httpPort,
                           const std::wstring& serverName,
                           const std::wstring& deviceUUID) {
    if (m_deviceHandle != -1) {
        return true;
    }

    const std::string ifName = [&]() -> std::string {
        const NetworkEndpoint* best = PickBestEndpoint(endpoints);
        if (best && !best->interfaceName.empty()) {
            return best->interfaceName;
        }
        return {};
    }();

    const NetworkEndpoint* bestEndpoint = PickBestEndpoint(endpoints);
    const std::string host = (bestEndpoint && !bestEndpoint->host.empty()) ? bestEndpoint->host : "127.0.0.1";
    m_httpAddr = host + ":" + std::to_string(httpPort);

    LogPrint(L"LibUPnPWrapper: init if=%hs http=%hs server=%ls uuid=%ls",
             ifName.c_str(),
             m_httpAddr.c_str(),
             serverName.c_str(),
             deviceUUID.c_str());

    const int initResult = UpnpInit2(ifName.empty() ? nullptr : ifName.c_str(), 0);
    if (initResult != UPNP_E_SUCCESS) {
        LogPrint(L"LibUPnPWrapper: UpnpInit2 failed: %d", initResult);
        return false;
    }

    const std::string descUrl = "http://" + m_httpAddr + "/description.xml";
    const int regResult = UpnpRegisterRootDevice2(UPNPREG_URL_DESC,
                                                  descUrl.c_str(),
                                                  0,
                                                  0,
                                                  LibUpnpCallback,
                                                  this,
                                                  &m_deviceHandle);
    if (regResult != UPNP_E_SUCCESS) {
        LogPrint(L"LibUPnPWrapper: UpnpRegisterRootDevice2 failed: %d", regResult);
        UpnpFinish();
        m_deviceHandle = -1;
        m_httpAddr.clear();
        return false;
    }

    const int advResult = UpnpSendAdvertisement(m_deviceHandle, 1800);
    if (advResult != UPNP_E_SUCCESS) {
        LogPrint(L"LibUPnPWrapper: UpnpSendAdvertisement failed: %d", advResult);
    }

    LogPrint(L"LibUPnPWrapper: started on %hs", m_httpAddr.c_str());
    return true;
}

void LibUPnPWrapper::Stop() {
    if (m_deviceHandle != -1) {
        UpnpUnRegisterRootDevice(m_deviceHandle);
        m_deviceHandle = -1;
    }
    UpnpFinish();
    m_httpAddr.clear();
}

#endif // _WIN32
