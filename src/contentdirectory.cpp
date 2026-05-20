#include "contentdirectory.h"
#include "config.h"
#include "dlna_utils.h"
#include "log.h"
#include "netutils.h"
#include <algorithm>
#include <sstream>
#include <vector>

namespace {
bool IsValidXml(const std::string& xml) {
    std::vector<std::string> tagStack;
    size_t i = 0;
    while (i < xml.size()) {
        size_t openBracket = xml.find('<', i);
        if (openBracket == std::string::npos) {
            if (xml.find('>', i) != std::string::npos) return false;
            break;
        }
        for (size_t check = i; check < openBracket; ++check) {
            if (xml[check] == '>') return false;
        }
        
        size_t closeBracket = xml.find('>', openBracket);
        if (closeBracket == std::string::npos) return false;
        
        std::string tagContent = xml.substr(openBracket + 1, closeBracket - openBracket - 1);
        i = closeBracket + 1;
        
        if (tagContent.empty()) return false;
        
        if (tagContent[0] == '?' || tagContent[0] == '!') {
            if (tagContent[0] == '?' && tagContent.back() != '?') return false;
            if (tagContent.rfind("!--", 0) == 0) {
                if (tagContent.size() < 5 || tagContent.compare(tagContent.size() - 2, 2, "--") != 0) {
                    size_t commentEnd = xml.find("-->", openBracket + 4);
                    if (commentEnd == std::string::npos) return false;
                    i = commentEnd + 3;
                }
            }
            continue;
        }
        
        if (tagContent.back() == '/') {
            continue;
        }
        
        if (tagContent[0] == '/') {
            std::string tagName = tagContent.substr(1);
            tagName.erase(tagName.find_last_not_of(" \t\r\n") + 1);
            if (tagStack.empty() || tagStack.back() != tagName) return false;
            tagStack.pop_back();
            continue;
        }
        
        size_t space = tagContent.find_first_of(" \t\r\n/");
        std::string tagName = (space == std::string::npos) ? tagContent : tagContent.substr(0, space);
        if (tagName.empty()) return false;
        tagStack.push_back(tagName);
    }
    return tagStack.empty();
}

bool ExtractTag(const std::string& req, const char* tag, std::string& value) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    size_t pos = req.find(open);
    size_t valueStart = 0;
    if (pos != std::string::npos) {
        valueStart = pos + open.size();
    } else {
        const std::string suffix = std::string(":") + tag + ">";
        pos = req.find(suffix);
        if (pos == std::string::npos) return false;
        if (req.rfind('<', pos) == std::string::npos) return false;
        valueStart = pos + suffix.size();
    }
    size_t endPos = req.find(close, valueStart);
    if (endPos == std::string::npos) {
        const std::string tagSuffix = std::string(":") + tag + ">";
        size_t closeStart = req.find("</", valueStart);
        while (closeStart != std::string::npos) {
            size_t closeEnd = req.find('>', closeStart);
            if (closeEnd == std::string::npos) break;
            std::string closeTag = req.substr(closeStart, closeEnd - closeStart + 1);
            if (closeTag.size() >= tagSuffix.size() &&
                closeTag.compare(closeTag.size() - tagSuffix.size(), tagSuffix.size(), tagSuffix) == 0) {
                endPos = closeStart;
                break;
            }
            closeStart = req.find("</", closeEnd + 1);
        }
    }
    if (endPos == std::string::npos) return false;
    value = req.substr(valueStart, endPos - valueStart);
    return true;
}

std::string SoapEnvelope(const std::string& body) {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
           "  <s:Body>\n" + body + "\n  </s:Body>\n</s:Envelope>";
}

std::string SoapFault(int code, const std::string& message) {
    std::stringstream ss;
    ss << "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring>"
       << "<detail><UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
       << "<errorCode>" << code << "</errorCode>"
       << "<errorDescription>" << XMLEscapeUtf8(message) << "</errorDescription>"
       << "</UPnPError></detail></s:Fault>";
    return SoapEnvelope(ss.str());
}

bool IsTitleSort(const std::string& criteria) {
    return ToLowerAscii(criteria).find("dc:title") != std::string::npos;
}

bool IsClassSort(const std::string& criteria) {
    return ToLowerAscii(criteria).find("upnp:class") != std::string::npos;
}

bool IsDescendingSort(const std::string& criteria) {
    return TrimAscii(criteria).rfind("-", 0) == 0;
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
    return "<?xml version=\"1.0\"?>\n"
           "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
           "<specVersion><major>1</major><minor>0</minor></specVersion>\n"
           "<actionList>\n"
           "<action><name>GetSystemUpdateID</name><argumentList><argument><name>Id</name><direction>out</direction><relatedStateVariable>SystemUpdateID</relatedStateVariable></argument></argumentList></action>\n"
           "<action><name>GetSearchCapabilities</name><argumentList><argument><name>SearchCaps</name><direction>out</direction><relatedStateVariable>SearchCapabilities</relatedStateVariable></argument></argumentList></action>\n"
           "<action><name>GetSortCapabilities</name><argumentList><argument><name>SortCaps</name><direction>out</direction><relatedStateVariable>SortCapabilities</relatedStateVariable></argument></argumentList></action>\n"
           "<action><name>Browse</name><argumentList>\n"
           "<argument><name>ObjectID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument>\n"
           "<argument><name>BrowseFlag</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable></argument>\n"
           "<argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument>\n"
           "<argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument>\n"
           "<argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
           "<argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument>\n"
           "<argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument>\n"
           "<argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
           "<argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
           "<argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument>\n"
           "</argumentList></action>\n"
           "</actionList>\n"
           "<serviceStateTable>\n"
           "<stateVariable sendEvents=\"yes\"><name>SystemUpdateID</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>SearchCapabilities</name><dataType>string</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>SortCapabilities</name><dataType>string</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType><allowedValueList><allowedValue>BrowseMetadata</allowedValue><allowedValue>BrowseDirectChildren</allowedValue></allowedValueList></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_UpdateID</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>\n"
           "</serviceStateTable>\n</scpd>";
}

std::string ContentDirectory::GetConnectionManagerXML() {
    return "<?xml version=\"1.0\"?>\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n<specVersion><major>1</major><minor>0</minor></specVersion>\n<actionList>\n<action>\n<name>GetProtocolInfo</name>\n<argumentList>\n<argument>\n<name>Source</name>\n<direction>out</direction>\n<relatedStateVariable>SourceProtocolInfo</relatedStateVariable>\n</argument>\n<argument>\n<name>Sink</name>\n<direction>out</direction>\n<relatedStateVariable>SinkProtocolInfo</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n<action>\n<name>GetCurrentConnectionIDs</name>\n<argumentList>\n<argument>\n<name>ConnectionIDs</name>\n<direction>out</direction>\n<relatedStateVariable>CurrentConnectionIDs</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n<action>\n<name>GetCurrentConnectionInfo</name>\n<argumentList>\n<argument>\n<name>ConnectionID</name>\n<direction>in</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>\n</argument>\n<argument>\n<name>RcsID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable>\n</argument>\n<argument>\n<name>AVTransportID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable>\n</argument>\n<argument>\n<name>ProtocolInfo</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable>\n</argument>\n<argument>\n<name>PeerConnectionManager</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable>\n</argument>\n<argument>\n<name>PeerConnectionID</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>\n</argument>\n<argument>\n<name>Direction</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable>\n</argument>\n<argument>\n<name>Status</name>\n<direction>out</direction>\n<relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable>\n</argument>\n</argumentList>\n</action>\n</actionList>\n<serviceStateTable>\n<stateVariable sendEvents=\"yes\">\n<name>SourceProtocolInfo</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"yes\">\n<name>SinkProtocolInfo</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"yes\">\n<name>CurrentConnectionIDs</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ConnectionStatus</name>\n<dataType>string</dataType>\n<allowedValueList>\n<allowedValue>OK</allowedValue>\n<allowedValue>ContentFormatMismatch</allowedValue>\n<allowedValue>InsufficientBandwidth</allowedValue>\n<allowedValue>UnreliableChannel</allowedValue>\n<allowedValue>Unknown</allowedValue>\n</allowedValueList>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ConnectionManager</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_Direction</name>\n<dataType>string</dataType>\n<allowedValueList>\n<allowedValue>Input</allowedValue>\n<allowedValue>Output</allowedValue>\n</allowedValueList>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ProtocolInfo</name>\n<dataType>string</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_ConnectionID</name>\n<dataType>i4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_AVTransportID</name>\n<dataType>i4</dataType>\n</stateVariable>\n<stateVariable sendEvents=\"no\">\n<name>A_ARG_TYPE_RcsID</name>\n<dataType>i4</dataType>\n</stateVariable>\n</serviceStateTable>\n</scpd>";
}

std::string ContentDirectory::HandleBrowse(const std::string& req, const std::string& hostUrl) {
    if (!IsValidXml(req)) {
        return SoapFault(401, "Invalid XML");
    }

    if (req.find("GetSystemUpdateID") != std::string::npos) {
        std::stringstream ss;
        ss << "    <u:GetSystemUpdateIDResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
           << "      <Id>" << AppMedia.GetSystemUpdateID() << "</Id>\n"
           << "    </u:GetSystemUpdateIDResponse>";
        return SoapEnvelope(ss.str());
    }

    if (req.find("GetSearchCapabilities") != std::string::npos) {
        return SoapEnvelope("    <u:GetSearchCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SearchCaps></SearchCaps></u:GetSearchCapabilitiesResponse>");
    }

    if (req.find("GetSortCapabilities") != std::string::npos) {
        return SoapEnvelope("    <u:GetSortCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SortCaps>dc:title,upnp:class</SortCaps></u:GetSortCapabilitiesResponse>");
    }

    std::string objIdStr;
    std::string browseFlag;
    std::string startingIndexStr;
    std::string requestedCountStr;
    if (!ExtractTag(req, "ObjectID", objIdStr) ||
        !ExtractTag(req, "BrowseFlag", browseFlag)) {
        return SoapFault(401, "Invalid XML");
    }
    if (!ExtractTag(req, "StartingIndex", startingIndexStr) ||
        !ExtractTag(req, "RequestedCount", requestedCountStr)) {
        return SoapFault(402, "Invalid Args");
    }

    int objId = 0;
    int startingIndex = 0;
    int requestedCount = 0;
    if (!TryParseIntStrict(objIdStr, objId) ||
        !TryParseIntStrict(startingIndexStr, startingIndex) ||
        !TryParseIntStrict(requestedCountStr, requestedCount) ||
        startingIndex < 0 ||
        requestedCount < 0) {
        return SoapFault(402, "Invalid Args");
    }

    std::vector<MediaItem> results;
    if (browseFlag == "BrowseMetadata") {
        MediaItem item = AppMedia.GetItem(objId);
        if (item.id == -1) return SoapFault(701, "No such object");
        results.push_back(item);
    } else if (browseFlag == "BrowseDirectChildren") {
        if (AppMedia.GetItem(objId).id == -1) return SoapFault(701, "No such object");
        results = AppMedia.GetChildren(objId);
    } else {
        return SoapFault(402, "Invalid Args");
    }

    std::string sortCriteria;
    ExtractTag(req, "SortCriteria", sortCriteria);
    if (IsTitleSort(sortCriteria) || IsClassSort(sortCriteria)) {
        const bool desc = IsDescendingSort(sortCriteria);
        const bool classSort = IsClassSort(sortCriteria);
        std::sort(results.begin(), results.end(), [desc, classSort](const MediaItem& a, const MediaItem& b) {
            const std::wstring& left = classSort ? a.upnpClass : a.title;
            const std::wstring& right = classSort ? b.upnpClass : b.title;
            if (left == right) return NaturalLessWide(a.title, b.title);
            bool less = NaturalLessWide(left, right);
            return desc ? !less : less;
        });
    }

    int returnCount = 0;
    int totalMatches = static_cast<int>(results.size());
    std::string didl;
    didl += "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns:sec=\"http://www.sec.co.kr/dlna\">";
    
    for (int i = startingIndex; i < static_cast<int>(results.size()) && (requestedCount == 0 || returnCount < requestedCount); ++i) {
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
       << "      <TotalMatches>" << (browseFlag == "BrowseMetadata" ? 1 : totalMatches) << "</TotalMatches>\n"
       << "      <UpdateID>" << AppMedia.GetSystemUpdateID() << "</UpdateID>\n"
       << "    </u:BrowseResponse>\n  </s:Body>\n</s:Envelope>";

    return ss.str();
}
