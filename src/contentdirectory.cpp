#include "contentdirectory.h"
#include "config.h"
#include "log.h"
#include "netutils.h"
#include <sstream>

namespace {
bool ExtractTag(const std::string& req, const char* tag, std::string& value) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    const size_t pos = req.find(open);
    if (pos == std::string::npos) return false;
    const size_t valueStart = pos + open.size();
    const size_t endPos = req.find(close, valueStart);
    if (endPos == std::string::npos) return false;
    value = req.substr(valueStart, endPos - valueStart);
    return true;
}

bool TryParseInt(const std::string& text, int& value) {
    try {
        size_t used = 0;
        int parsed = std::stoi(text, &used);
        if (used != text.size()) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}
}

ContentDirectory& ContentDirectory::Get() {
    static ContentDirectory instance;
    return instance;
}

std::string ContentDirectory::XMLEscape(const std::wstring& wstr) {
    return XMLEscapeUtf8(WideToUtf8(wstr));
}

std::string ContentDirectory::GetDeviceDescriptionXML() {
    auto& cfg = AppConfig;
    std::string deviceUUID = WideToUtf8(cfg.deviceUUID);
    std::string serverName = XMLEscapeUtf8(WideToUtf8(cfg.serverName));

    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
       << "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">\n"
       << "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
       << "  <device>\n"
       << "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"
       << "    <friendlyName>" << serverName << "</friendlyName>\n"
       << "    <manufacturer>CustomDLNA</manufacturer>\n"
       << "    <modelName>WinDLNAServer</modelName>\n"
       << "    <UDN>uuid:" << deviceUUID << "</UDN>\n"
       << "    <dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMS-1.50</dlna:X_DLNADOC>\n"
       << "    <serviceList>\n"
       << "      <service>\n"
       << "        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>\n"
       << "        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>\n"
       << "        <SCPDURL>/ContentDirectory.xml</SCPDURL>\n"
       << "        <controlURL>/upnp/control/content_directory</controlURL>\n"
       << "        <eventSubURL>/upnp/event/content_directory</eventSubURL>\n"
       << "      </service>\n"
       << "      <service>\n"
       << "        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>\n"
       << "        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>\n"
       << "        <SCPDURL>/ConnectionManager.xml</SCPDURL>\n"
       << "        <controlURL>/upnp/control/connection_manager</controlURL>\n"
       << "        <eventSubURL>/upnp/event/connection_manager</eventSubURL>\n"
       << "      </service>\n"
       << "    </serviceList>\n"
       << "  </device>\n"
       << "</root>";
    return ss.str();
}

std::string ContentDirectory::GetContentDirectoryXML() {
    return "<?xml version=\"1.0\"?>\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n<specVersion><major>1</major><minor>0</minor></specVersion>\n<actionList>\n<action>\n<name>GetSystemUpdateID</name>\n<argumentList>\n<argument>\n<name>Id</name>\n<direction>out</direction>\n<relatedStateVariable>SystemUpdateID</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n<action>\n<name>Browse</name>\n<argumentList>\n<argument>\n<name>ObjectID</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable>\n</argument>\n<argument>\n<name>BrowseFlag</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable>\n</argument>\n<argument>\n<name>Filter</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable>\n</argument>\n<argument>\n<name>StartingIndex</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable>\n</argument>\n<argument>\n<name>RequestedCount</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\n</argument>\n<argument>\n<name>SortCriteria</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable>\n</argument>\n<argument>\n<name>Result</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable>\n</argument>\n<argument>\n<name>NumberReturned</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\n</argument>\n<argument>\n<name>TotalMatches</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>\n</argument>\n<argument>\n<name>UpdateID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n</actionList>\n<serviceStateTable>\n<stateVariable sendEvents=\"yes\">\n<name>SystemUpdateID</name>\n<dataType>ui4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ObjectID</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_BrowseFlag</name>\n<dataType>string</dataType>\n<allowedValueList>\n<allowedValue>BrowseMetadata</allowedValue>\n<allowedValue>BrowseDirectChildren</allowedValue>\n</allowedValueList>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_Filter</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_SortCriteria</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_Index</name>\n<dataType>ui4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_Count</name>\n<dataType>ui4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_UpdateID</name>\n<dataType>ui4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_Result</name>\n<dataType>string</dataType>\n</stateVariable>\n</serviceStateTable>\n</scpd>";
}

std::string ContentDirectory::GetConnectionManagerXML() {
    return "<?xml version=\"1.0\"?>\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n<specVersion><major>1</major><minor>0</minor></specVersion>\n<actionList>\n<action>\n<name>GetProtocolInfo</name>\n<argumentList>\n<argument>\n<name>Source</name>\n<direction>out</direction>\n<relatedStateVariable>SourceProtocolInfo</relatedStateVariable>\n</argument>\n<argument>\n<name>Sink</name>\n<direction>out</direction>\n<relatedStateVariable>SinkProtocolInfo</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n<action>\n<name>GetCurrentConnectionIDs</name>\n<argumentList>\n<argument>\n<name>ConnectionIDs</name>\n<direction>out</direction>\n<relatedStateVariable>CurrentConnectionIDs</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n<action>\n<name>GetCurrentConnectionInfo</name>\n<argumentList>\n<argument>\n<name>ConnectionID</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>\n</argument>\n<argument>\n<name>RcsID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable>\n</argument>\n<argument>\n<name>AVTransportID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable>\n</argument>\n<argument>\n<name>ProtocolInfo</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable>\n</argument>\n<argument>\n<name>PeerConnectionManager</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable>\n</argument>\n<argument>\n<name>PeerConnectionID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>\n</argument>\n<argument>\n<name>Direction</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable>\n</argument>\n<argument>\n<name>Status</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n</actionList>\n<serviceStateTable>\n<stateVariable sendEvents=\"yes\">\n<name>SourceProtocolInfo</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"yes\">\n<name>SinkProtocolInfo</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"yes\">\n<name>CurrentConnectionIDs</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ConnectionStatus</name>\n<dataType>string</dataType>\n<allowedValueList>\n<allowedValue>OK</allowedValue>\n<allowedValue>ContentFormatMismatch</allowedValue>\n<allowedValue>InsufficientBandwidth</allowedValue>\n<allowedValue>UnreliableChannel</allowedValue>\n<allowedValue>Unknown</allowedValue>\n</allowedValueList>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ConnectionManager</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_Direction</name>\n<dataType>string</dataType>\n<allowedValueList>\n<allowedValue>Input</allowedValue>\n<allowedValue>Output</allowedValue>\n</allowedValueList>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ProtocolInfo</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ConnectionID</name>\n<dataType>i4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_AVTransportID</name>\n<dataType>i4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_RcsID</name>\n<dataType>i4</dataType>\n</stateVariable>\n</serviceStateTable>\n</scpd>";
}

std::string ContentDirectory::HandleBrowse(const std::string& req, const std::string& hostUrl) {
    if (req.find("GetSystemUpdateID") != std::string::npos) {
        std::stringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
           << "  <s:Body>\n    <u:GetSystemUpdateIDResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
           << "      <Id>" << AppMedia.GetSystemUpdateID() << "</Id>\n"
           << "    </u:GetSystemUpdateIDResponse>\n  </s:Body>\n</s:Envelope>";
        return ss.str();
    }

    std::string objIdStr;
    std::string browseFlag;
    std::string startingIndexStr;
    std::string requestedCountStr;
    if (!ExtractTag(req, "ObjectID", objIdStr) ||
        !ExtractTag(req, "BrowseFlag", browseFlag) ||
        !ExtractTag(req, "StartingIndex", startingIndexStr) ||
        !ExtractTag(req, "RequestedCount", requestedCountStr)) {
        return "";
    }

    int objId = 0;
    int startingIndex = 0;
    int requestedCount = 0;
    if (!TryParseInt(objIdStr, objId) ||
        !TryParseInt(startingIndexStr, startingIndex) ||
        !TryParseInt(requestedCountStr, requestedCount) ||
        startingIndex < 0 ||
        requestedCount < 0) {
        return "";
    }

    std::vector<MediaItem> results;
    if (browseFlag == "BrowseMetadata") {
        results.push_back(AppMedia.GetItem(objId));
    } else {
        results = AppMedia.GetChildren(objId);
    }

    int returnCount = 0;
    std::string didl;
    didl += "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns:sec=\"http://www.sec.co.kr/dlna\">";
    
    for (int i = startingIndex; i < results.size() && (requestedCount == 0 || returnCount < requestedCount); ++i) {
        const auto& it = results[i];
        if (it.id == -1) continue;
        
        if (it.isFolder) {
            std::stringstream css;
            int childCount = AppMedia.GetChildren(it.id).size();
            css << "<container id=\"" << it.id << "\" parentID=\"" << it.parentId << "\" childCount=\"" << childCount << "\" restricted=\"1\">"
                << "<dc:title>" << XMLEscape(it.title) << "</dc:title>"
                << "<upnp:class>" << XMLEscape(it.upnpClass) << "</upnp:class>"
                << "</container>";
            didl += css.str();
        } else {
            std::string mime = WideToUtf8(it.mimeType);
            std::stringstream iss;
            iss << "<item id=\"" << it.id << "\" parentID=\"" << it.parentId << "\" restricted=\"1\">"
                << "<dc:title>" << XMLEscape(it.title) << "</dc:title>"
                << "<upnp:class>" << XMLEscape(it.upnpClass) << "</upnp:class>"
                << "<res protocolInfo=\"http-get:*:" << mime << ":DLNA.ORG_OP=01;DLNA.ORG_FLAGS=01700000000000000000000000000000\" size=\"" << it.sizeBytes << "\">"
                << "http://" << hostUrl << "/media/" << it.id
                << "</res>";
            // emit subtitle reference if a companion file was found during scan
            // vlc and samsung renderers read sec:CaptionInfoEx to locate external subs
            if (!it.subtitlePath.empty()) {
                std::wstring subExtW = it.subtitlePath.substr(it.subtitlePath.rfind(L'.') + 1);
                std::string subExt = WideToUtf8(subExtW);
                iss << "<sec:CaptionInfoEx sec:type=\"" << subExt << "\">"
                    << "http://" << hostUrl << "/subtitle/" << it.id
                    << "</sec:CaptionInfoEx>";
            }
            iss << "</item>";
            didl += iss.str();
        }
        returnCount++;
    }
    didl += "</DIDL-Lite>";

    std::string escapedDidl = XMLEscapeUtf8(didl);

    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
       << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
       << "  <s:Body>\n    <u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
       << "      <Result>" << escapedDidl << "</Result>\n"
       << "      <NumberReturned>" << returnCount << "</NumberReturned>\n"
       << "      <TotalMatches>" << results.size() << "</TotalMatches>\n"
       << "      <UpdateID>" << AppMedia.GetSystemUpdateID() << "</UpdateID>\n"
       << "    </u:BrowseResponse>\n  </s:Body>\n</s:Envelope>";

    return ss.str();
}
