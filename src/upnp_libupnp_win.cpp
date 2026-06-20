#include "upnp_libupnp_win.h"

#ifdef _WIN32

#include "contentdirectory.h"
#include "media_sources.h"
#include "dlna_utils.h"
#include "log.h"
#include "config.h"
#include <upnp/upnp.h>
#include <upnp/upnptools.h>
#include <upnp/ixml.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
    std::string ExtractActionRequestXML(IXML_Document* doc) {
        if (!doc) return "";
        DOMString str = ixmlPrintDocument(doc);
        if (!str) return "";
        std::string res = str;
        ixmlFreeDOMString(str);
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>" + res + "</s:Body></s:Envelope>";
    }

    int UpnpCallback(Upnp_EventType EventType, const void* Event, void* Cookie) {
        LibUPnPWrapper* self = static_cast<LibUPnPWrapper*>(Cookie);
        if (EventType == UPNP_CONTROL_ACTION_REQUEST) {
            Upnp_Action_Request* req = (Upnp_Action_Request*)Event;
            std::string actionName = req->ActionName;
            std::string serviceId = req->ServiceID;
            std::string xmlReq = ExtractActionRequestXML(req->ActionRequest);
            
            std::string respXml;
            if (serviceId == "urn:upnp-org:serviceId:ContentDirectory") {
                respXml = AppContent.HandleContentDirectoryControl(xmlReq, self->GetHttpAddr());
            } else if (serviceId == "urn:upnp-org:serviceId:ConnectionManager") {
                respXml = AppContent.HandleConnectionManagerControl(xmlReq);
            }
            
            if (!respXml.empty()) {
                IXML_Document* respDoc = ixmlParseBuffer(respXml.c_str());
                if (respDoc) {
                    IXML_NodeList* bodyList = ixmlDocument_getElementsByTagName(respDoc, "s:Body");
                    if (bodyList) {
                        IXML_Node* bodyNode = ixmlNodeList_item(bodyList, 0);
                        if (bodyNode) {
                            IXML_Node* actionRespNode = ixmlNode_getFirstChild(bodyNode);
                            if (actionRespNode) {
                                IXML_Node* cloned = ixmlNode_cloneNode(actionRespNode, TRUE);
                                IXML_Document* newDoc = ixmlDocument_createDocument();
                                ixmlNode_appendChild((IXML_Node*)newDoc, cloned);
                                req->ActionResult = newDoc;
                                req->ErrCode = UPNP_E_SUCCESS;
                            }
                        }
                        ixmlNodeList_free(bodyList);
                    }
                    ixmlDocument_free(respDoc);
                }
                
                if (!req->ActionResult) {
                    req->ErrCode = UPNP_E_INTERNAL_ERROR;
                }
            } else {
                req->ErrCode = UPNP_E_INVALID_ACTION;
            }
            return 0;
        } else if (EventType == UPNP_EVENT_SUBSCRIPTION_REQUEST) {
            Upnp_Subscription_Request* req = (Upnp_Subscription_Request*)Event;
            UpnpAcceptSubscription(m_deviceHandle, req->DevUDN, req->ServiceID, (const char**)nullptr, (const char**)nullptr, 0, req->Sid);
            // wait, UpnpAcceptSubscription signature in modern libupnp is:
            // int UpnpAcceptSubscription(UpnpDevice_Handle Hnd, const char *DevID, const char *ServId, const char **VarName, const char **NewVal, int cVariables, const char *SubsId);
            return 0;
        }
        return 0;
    }
}

LibUPnPWrapper& LibUPnPWrapper::Get() {
    static LibUPnPWrapper instance;
    return instance;
}

bool LibUPnPWrapper::Start(const std::vector<NetworkEndpoint>& endpoints, int httpPort, const std::wstring& serverName, const std::wstring& deviceUUID) {
    if (m_deviceHandle != -1) return true;

    LogPrint(L"LibUPnPWrapper: calling UpnpInit2");
    int ret = UpnpInit2(nullptr, 0);
    if (ret != UPNP_E_SUCCESS) {
        LogPrint(L"UpnpInit2 failed: %d", ret);
        return false;
    }
    LogPrint(L"UpnpInit2 ok, port=%d", UpnpGetServerPort());

    m_tempDir = std::filesystem::temp_directory_path().string() + "\\dlna_server_xml";
    std::error_code ec;
    std::filesystem::remove_all(m_tempDir, ec);
    std::filesystem::create_directories(m_tempDir);
    std::filesystem::create_directories(m_tempDir + "\\icons");

    std::string ip = "127.0.0.1";
    if (!endpoints.empty()) ip = endpoints[0].address;
    m_httpAddr = ip + ":" + std::to_string(httpPort);

    LogPrint(L"LibUPnPWrapper: writing XML to %hs", m_tempDir.c_str());
    std::string deviceXml = AppContent.GetDeviceDescriptionXML();

    std::ofstream(m_tempDir + "\\description.xml") << deviceXml;
    std::ofstream(m_tempDir + "\\ContentDirectory.xml") << AppContent.GetContentDirectoryXML();
    std::ofstream(m_tempDir + "\\ConnectionManager.xml") << AppContent.GetConnectionManagerXML();

    UpnpSetWebServerRootDir(m_tempDir.c_str());

    // libupnp requires an HTTP URL, not a local file path
    unsigned short upnpPort = UpnpGetServerPort();
    const char* upnpIp = UpnpGetServerIpAddress();
    std::string deviceXmlUrl = "http://";
    deviceXmlUrl += (upnpIp ? upnpIp : "127.0.0.1");
    deviceXmlUrl += ":";
    deviceXmlUrl += std::to_string(upnpPort);
    deviceXmlUrl += "/description.xml";
    LogPrint(L"LibUPnPWrapper: registering root device at %hs", deviceXmlUrl.c_str());
    ret = UpnpRegisterRootDevice(deviceXmlUrl.c_str(), UpnpCallback, this, &m_deviceHandle);
    if (ret != UPNP_E_SUCCESS) {
        LogPrint(L"UpnpRegisterRootDevice failed: %d", ret);
        UpnpFinish();
        return false;
    }
    LogPrint(L"LibUPnPWrapper: device registered, sending advertisement");

    UpnpSendAdvertisement(m_deviceHandle, 1800);
    LogPrint(L"LibUPnPWrapper: started ok");
    return true;
}

void LibUPnPWrapper::Stop() {
    if (m_deviceHandle != -1) {
        UpnpUnRegisterRootDevice(m_deviceHandle);
        m_deviceHandle = -1;
    }
    UpnpFinish();
}

std::string LibUPnPWrapper::GetHttpAddr() const {
    return m_httpAddr;
}

// Dummy UpnpEventManager to satisfy linking
class UpnpEventManager {
public:
    static UpnpEventManager& Get() { static UpnpEventManager inst; return inst; }
    void NotifySystemUpdateId(int updateId) {
        LibUPnPWrapper::Get().NotifyUpdateId(updateId);
    }
};

void LibUPnPWrapper::NotifyUpdateId(int updateId) {
    if (m_deviceHandle != -1) {
        const char* names[] = { "SystemUpdateID" };
        std::string val = std::to_string(updateId);
        const char* vals[] = { val.c_str() };
        // We need the UDN from config.
        const ConfigSnapshot cfg = AppConfig.Snapshot();
        std::string udn = "uuid:" + WideToUtf8(cfg.deviceUUID);
        UpnpNotify(m_deviceHandle, udn.c_str(), "urn:upnp-org:serviceId:ContentDirectory", names, vals, 1);
    }
}

// Global scope
UpnpEventManager& UpnpEventManager::Get() {
    static UpnpEventManager instance;
    return instance;
}

#endif // _WIN32
