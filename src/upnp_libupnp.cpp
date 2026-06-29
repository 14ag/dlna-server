#include "upnp_libupnp.h"
#include "contentdirectory.h"

#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <upnp/upnp.h>

namespace {
int LibUpnpCallback(Upnp_EventType, void*, void*) {
    return 0;
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

    const NetworkEndpoint* bindEndpoint = SelectHostingEndpoint(endpoints);
    if (bindEndpoint == nullptr || bindEndpoint->interfaceName.empty()) {
        LogPrint(L"LibUPnPWrapper: no usable interface for UpnpInit2.");
        return false;
    }

    const std::string ifName = bindEndpoint->interfaceName;
    const std::string ifAddr = bindEndpoint->address;
    m_httpAddr = ifAddr + ":" + std::to_string(httpPort);

    LogPrint(L"LibUPnPWrapper: init if=%hs addr=%hs http=%hs server=%ls uuid=%ls",
             ifName.c_str(),
             ifAddr.c_str(),
             m_httpAddr.c_str(),
             serverName.c_str(),
             deviceUUID.c_str());

    // Try the friendly interface name first; fall back to NULL (let libupnp pick)
    // if the name lookup fails. On Windows, libupnp resolves by friendly name.
    int initResult = UpnpInit2(ifName.c_str(), 0);
    if (initResult == UPNP_E_INVALID_INTERFACE) {
        LogPrint(L"LibUPnPWrapper: named interface failed, retrying with NULL");
        initResult = UpnpInit2(nullptr, 0);
    }
    if (initResult != UPNP_E_SUCCESS) {
        LogPrint(L"LibUPnPWrapper: UpnpInit2 failed: %d", initResult);
        m_httpAddr.clear();
        return false;
    }

    const char* actualIp = UpnpGetServerIpAddress();
    const unsigned short actualPort = UpnpGetServerPort();
    LogPrint(L"LibUPnPWrapper: server ip=%hs port=%hu",
             actualIp ? actualIp : "(null)", actualPort);

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
        UpnpFinish();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    m_httpAddr.clear();
}

std::string LibUPnPWrapper::GetHttpAddr() const {
    return m_httpAddr;
}

void LibUPnPWrapper::NotifyUpdateId(int updateId) {
    const ConfigSnapshot cfg = AppConfig.Snapshot();
    LogPrint(L"LibUPnPWrapper: system update id=%d uuid=%ls", updateId, cfg.deviceUUID.c_str());
    if (m_deviceHandle != -1) {
        UpnpSendAdvertisement(m_deviceHandle, 1800);
    }
}
