#include "contentdirectory.h"
#include "config.h"
#include "dlna_utils.h"
#include "version.h"
#include "log.h"
#include "netutils.h"
#include "network_sources.h"
#include "server.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <vector>
#include <cwctype>

namespace fs = std::filesystem;

namespace {
size_t FindXmlTagEnd(const std::string& xml, size_t openAt) {
    char quote = '\0';
    for (size_t i = openAt + 1; i < xml.size(); ++i) {
        const char ch = xml[i];
        if (quote != '\0') {
            if (ch == quote) quote = '\0';
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '>') return i;
    }
    return std::string::npos;
}

std::string LocalXmlName(std::string name) {
    const size_t colon = name.find(':');
    if (colon != std::string::npos) name = name.substr(colon + 1);
    return name;
}

bool IsSoapWrapperTag(const std::string& localName) {
    return localName == "Envelope" || localName == "Body";
}

std::string ExtractSoapActionName(const std::string& xml, bool& malformed) {
    malformed = false;
    size_t pos = 0;
    while (pos < xml.size()) {
        const size_t openAt = xml.find('<', pos);
        if (openAt == std::string::npos) return {};

        if (xml.compare(openAt, 4, "<!--") == 0) {
            const size_t end = xml.find("-->", openAt + 4);
            if (end == std::string::npos) {
                malformed = true;
                return {};
            }
            pos = end + 3;
            continue;
        }
        if (xml.compare(openAt, 9, "<![CDATA[") == 0) {
            const size_t end = xml.find("]]>", openAt + 9);
            if (end == std::string::npos) {
                malformed = true;
                return {};
            }
            pos = end + 3;
            continue;
        }

        const size_t closeAt = FindXmlTagEnd(xml, openAt);
        if (closeAt == std::string::npos) {
            malformed = true;
            return {};
        }

        std::string tag = TrimAscii(xml.substr(openAt + 1, closeAt - openAt - 1));
        pos = closeAt + 1;
        if (tag.empty()) {
            malformed = true;
            return {};
        }
        if (tag[0] == '/' || tag[0] == '?' || tag[0] == '!') {
            continue;
        }
        const size_t nameEnd = tag.find_first_of(" \t\r\n/");
        std::string localName = LocalXmlName(nameEnd == std::string::npos ? tag : tag.substr(0, nameEnd));
        if (localName.empty()) {
            malformed = true;
            return {};
        }
        if (!IsSoapWrapperTag(localName)) {
            return localName;
        }
    }
    return {};
}

bool FindStartTagWithAttributes(const std::string& req, const char* tag, size_t from, size_t& tagStart, size_t& valueStart) {
    size_t pos = from;
    while (pos < req.size()) {
        tagStart = req.find('<', pos);
        if (tagStart == std::string::npos) return false;
        const size_t tagEnd = FindXmlTagEnd(req, tagStart);
        if (tagEnd == std::string::npos) return false;
        std::string body = TrimAscii(req.substr(tagStart + 1, tagEnd - tagStart - 1));
        pos = tagEnd + 1;
        if (body.empty() || body[0] == '/' || body[0] == '!' || body[0] == '?') continue;
        const size_t nameEnd = body.find_first_of(" \t\r\n/");
        const std::string name = LocalXmlName(nameEnd == std::string::npos ? body : body.substr(0, nameEnd));
        if (name == tag) {
            if (!body.empty() && body.back() == '/') return false;
            valueStart = tagEnd + 1;
            return true;
        }
    }
    return false;
}

bool ExtractTagValue(const std::string& req, const char* tag, std::string& value) {
    size_t tagStart = 0;
    size_t valueStart = 0;
    if (!FindStartTagWithAttributes(req, tag, 0, tagStart, valueStart)) return false;

    size_t pos = valueStart;
    while (pos < req.size()) {
        const size_t closeStart = req.find("</", pos);
        if (closeStart == std::string::npos) return false;
        const size_t closeEnd = FindXmlTagEnd(req, closeStart);
        if (closeEnd == std::string::npos) return false;
        std::string body = TrimAscii(req.substr(closeStart + 2, closeEnd - closeStart - 2));
        const size_t nameEnd = body.find_first_of(" \t\r\n");
        const std::string name = LocalXmlName(nameEnd == std::string::npos ? body : body.substr(0, nameEnd));
        if (name == tag) {
            value = req.substr(valueStart, closeStart - valueStart);
            return true;
        }
        pos = closeEnd + 1;
    }
    return false;
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

std::string EscapeWide(const std::wstring& value) {
    return XMLEscapeUtf8(WideToUtf8(value));
}

bool ShouldProxyRemoteUrl(const std::wstring& url) {
    std::string text = WideToUtf8(url);
    size_t schemeEnd = text.find("://");
    if (schemeEnd == std::string::npos) return true;
    std::string scheme = text.substr(0, schemeEnd);
    if (scheme != "http" && scheme != "https") return true;
    size_t authorityStart = schemeEnd + 3;
    size_t authorityEnd = text.find_first_of("?/#", authorityStart);
    size_t at = text.find('@', authorityStart);
    if (at != std::string::npos && (authorityEnd == std::string::npos || at < authorityEnd)) {
        return true;
    }
    return false;
}

std::string ItemProtocolInfo(const MediaItem& item) {
    // HLS manifests must not go through kFormats (which excludes .m3u8 intentionally).
    // Emit OP=01 (time-seek available) and CI=0 matching the android proxy pattern
    // from j.java contentFeatures.dlna.org header. OP=00 (no seek) causes strict
    // DLNA renderers to reject the <res> element and show the item as empty.
    if (item.mimeType == L"video/mpegurl") {
        return BuildHlsProtocolInfo();
    }
    return BuildProtocolInfoForExtension(SourceExtension(item.path), item.mimeType, item.sizeBytes > 0);
}

void SortItems(std::vector<MediaItem>& items, const std::string& sortCriteria) {
    if (IsTitleSort(sortCriteria) || IsClassSort(sortCriteria) || (sortCriteria.empty() && AppConfig.IsSortByTitleEnabled())) {
        const bool desc = IsDescendingSort(sortCriteria);
        const bool classSort = IsClassSort(sortCriteria);
        std::sort(items.begin(), items.end(), [desc, classSort](const MediaItem& a, const MediaItem& b) {
            const std::wstring& left = classSort ? a.upnpClass : a.title;
            const std::wstring& right = classSort ? b.upnpClass : b.title;
            if (left == right) return NaturalLessWide(a.title, b.title);
            bool less = NaturalLessWide(left, right);
            return desc ? !less : less;
        });
    }
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    std::wstring h = haystack;
    std::wstring n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    std::transform(n.begin(), n.end(), n.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return h.find(n) != std::wstring::npos;
}

bool StartsWithNoCase(const std::wstring& value, const std::wstring& prefix) {
    if (value.size() < prefix.size()) return false;
    return ContainsNoCase(value.substr(0, prefix.size()), prefix);
}

std::string ExtractQuotedCriteriaValue(const std::string& criteria) {
    size_t first = criteria.find('"');
    size_t last = criteria.rfind('"');
    if (first == std::string::npos || last == first) {
        first = criteria.find('\'');
        last = criteria.rfind('\'');
    }
    if (first == std::string::npos || last == first) return {};
    return criteria.substr(first + 1, last - first - 1);
}

// Supports exactly one of: dc:title contains "...", upnp:class derivedfrom
// "...", or upnp:class = "...". Per the ContentDirectory:1 spec's Search
// Criteria String Syntax (Appendix B), a SearchCriteria string may combine
// multiple property expressions with "and"/"or" -- that combined form is
// NOT parsed here and falls through to the final return false below,
// meaning a combined-criteria Search silently returns zero matches instead
// of a partial or best-effort match. This is a known, accepted limitation:
// the DLNA renderers this project targets in practice send single-clause
// criteria almost exclusively (title-contains or class-derivedFrom), and
// implementing a real boolean-expression parser for the full grammar is a
// larger scope than this function's current callers need. If a renderer
// in the wild is observed sending combined criteria and getting an
// unexpectedly empty result, that is this code path -- start here.
bool MatchesSearchCriteria(const MediaItem& item, const std::string& criteria) {
    const std::string normalized = ToLowerAscii(TrimAscii(criteria));
    if (normalized.empty() || normalized == "*" || normalized == "true") return true;
    if (normalized.find("dc:title") != std::string::npos && normalized.find("contains") != std::string::npos) {
        std::string needle = ExtractQuotedCriteriaValue(criteria);
        return needle.empty() ? true : ContainsNoCase(item.title, Utf8ToWide(needle));
    }
    if (normalized.find("upnp:class") != std::string::npos && normalized.find("derivedfrom") != std::string::npos) {
        std::string value = ExtractQuotedCriteriaValue(criteria);
        return value.empty() ? true : StartsWithNoCase(item.upnpClass, Utf8ToWide(value));
    }
    const bool classEquals = normalized.find("upnp:class =") != std::string::npos ||
                             normalized.find("upnp:class=") != std::string::npos;
    if (classEquals) {
        std::string value = ExtractQuotedCriteriaValue(criteria);
        return value.empty() ? true : ToLowerWide(item.upnpClass) == ToLowerWide(Utf8ToWide(value));
    }
    return false;
}

bool ApplyDidlFilter(const std::string& filter, const char* field) {
    const std::string normalized = ToLowerAscii(TrimAscii(filter));
    if (normalized.empty() || normalized == "*") return true;
    return normalized.find(ToLowerAscii(field)) != std::string::npos;
}

std::string BuildDIDL(const std::vector<MediaItem>& items, int startingIndex, int requestedCount, const std::string& hostUrl, const std::string& filter, int& returnCount) {
    returnCount = 0;
    const int safeStart = (std::max)(0, startingIndex);
    const int available = safeStart >= static_cast<int>(items.size()) ? 0 : static_cast<int>(items.size()) - safeStart;
    const int requested = requestedCount == 0 ? available : (std::min)(requestedCount, available);
    const auto childCounts = AppMedia.GetChildCounts(items);
    const bool proxyStreams = AppConfig.IsProxyStreamsEnabled();
    const bool includeTitle = ApplyDidlFilter(filter, "dc:title");
    const bool includeClass = ApplyDidlFilter(filter, "upnp:class");
    const bool includeAlbumArt = ApplyDidlFilter(filter, "upnp:albumArtURI");
    const bool includeResource = ApplyDidlFilter(filter, "res");
    const bool includeDate = ApplyDidlFilter(filter, "dc:date");
    std::string didl = "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\" xmlns:sec=\"http://www.sec.co.kr/dlna\">";
    didl.reserve(256 + (static_cast<size_t>((std::max)(0, requested)) * 512));
    std::ostringstream entry;
    for (int i = safeStart; i < static_cast<int>(items.size()) && returnCount < requested; ++i) {
        const auto& it = items[i];
        if (it.id == -1) continue;
        entry.str(std::string());
        entry.clear();
        if (it.isFolder) {
            auto countIt = childCounts.find(it.id);
            int childCount = countIt == childCounts.end() ? 0 : countIt->second;
            entry << "<container id=\"" << it.id << "\" parentID=\"" << it.parentId << "\" childCount=\"" << childCount << "\" restricted=\"1\">"
                  << (includeTitle ? ("<dc:title>" + EscapeWide(it.title) + "</dc:title>") : std::string())
                  << (includeClass ? ("<upnp:class>" + EscapeWide(it.upnpClass) + "</upnp:class>") : std::string())
                  << "</container>";
            didl += entry.str();
        } else {
            const bool hasKnownSize = it.sizeBytes > 0;
            entry << "<item id=\"" << it.id << "\" parentID=\"" << it.parentId << "\" restricted=\"1\"";
            if (!it.dcDate.empty() && it.rawDateMs > 0) {
                entry << " rawDate=\"" << it.rawDateMs << "\"";
            }
            entry << ">"
                  << (includeTitle ? ("<dc:title>" + EscapeWide(it.title) + "</dc:title>") : std::string())
                  << (includeClass ? ("<upnp:class>" + EscapeWide(it.upnpClass) + "</upnp:class>") : std::string());
            if (includeDate && !it.dcDate.empty()) {
                entry << "<dc:date>" << it.dcDate << "</dc:date>";
            }
            if (includeAlbumArt && !it.albumArtPath.empty()) {
                entry << "<upnp:albumArtURI dlna:profileID=\"JPEG_TN\">http://" << hostUrl << "/albumart/" << it.id << "</upnp:albumArtURI>";
            }
            if (includeResource) {
                entry << "<res protocolInfo=\"" << ItemProtocolInfo(it) << "\"";
                if (hasKnownSize) {
                    entry << " size=\"" << it.sizeBytes << "\"";
                }
                const bool exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !proxyStreams && !ShouldProxyRemoteUrl(it.path);
                entry << ">" << (exposeRemoteDirect ? XMLEscapeUtf8(WideToUtf8(it.path)) : ("http://" + hostUrl + "/media/" + std::to_string(it.id))) << "</res>";
            }
            if (!it.subtitlePath.empty()) {
                std::wstring subExtW = SourceExtension(it.subtitlePath);
                if (!subExtW.empty() && subExtW.front() == L'.') subExtW.erase(0, 1);
                std::string subExt = WideToUtf8(subExtW);
                entry << "<sec:CaptionInfoEx sec:type=\"" << subExt << "\">"
                      << "http://" << hostUrl << "/subtitle/" << it.id
                      << "</sec:CaptionInfoEx>";
            }
            entry << "</item>";
            didl += entry.str();
        }
        returnCount++;
    }
    didl += "</DIDL-Lite>";
    return didl;
}

std::string BrowseSearchResponse(const std::string& actionName, const std::vector<MediaItem>& items, int startingIndex, int requestedCount, const std::string& hostUrl, const std::string& filter, int totalMatches) {
    int returnCount = 0;
    std::string didl = BuildDIDL(items, startingIndex, requestedCount, hostUrl, filter, returnCount);
    std::stringstream ss;
    ss << "    <u:" << actionName << "Response xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
       << "      <Result>" << XMLEscapeUtf8(didl) << "</Result>\n"
       << "      <NumberReturned>" << returnCount << "</NumberReturned>\n"
       << "      <TotalMatches>" << totalMatches << "</TotalMatches>\n"
       << "      <UpdateID>" << AppMedia.GetSystemUpdateID() << "</UpdateID>\n"
       << "    </u:" << actionName << "Response>";
    return SoapEnvelope(ss.str());
}
}

ContentDirectory& ContentDirectory::Get() {
    static ContentDirectory instance;
    return instance;
}

std::string ContentDirectory::GetDeviceDescriptionXML(const std::string& hostUrl) {
    const DeviceDescriptionConfig cfg = AppConfig.GetDeviceDescriptionConfig();
    std::string deviceUUID = XMLEscapeUtf8(WideToUtf8(cfg.deviceUUID));
    std::string serverName = XMLEscapeUtf8(WideToUtf8(cfg.serverName));
    std::string manufacturer = XMLEscapeUtf8(WideToUtf8(cfg.deviceManufacturer));
    std::string modelName = XMLEscapeUtf8(WideToUtf8(cfg.deviceModelName));
    std::string presentationUrl = XMLEscapeUtf8(WideToUtf8(cfg.presentationUrl));
    std::stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
       << "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">\n"
       << "  <URLBase>http://" << hostUrl << "</URLBase>\n";
    ss << "  <device>\n"
       << "    <specVersion><major>1</major><minor>0</minor></specVersion>\n"
       << "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"
       << "    <friendlyName>" << serverName << "</friendlyName>\n"
       << "    <manufacturer>" << manufacturer << "</manufacturer>\n"
       << "    <modelName>" << modelName << "</modelName>\n"
       << "    <modelNumber>" DLNA_SERVER_VERSION_STRING "</modelNumber>\n"
       << "    <modelURL>https://github.com/14ag/dlna-server</modelURL>\n"
       << "    <manufacturerURL>https://github.com/14ag/dlna-server</manufacturerURL>\n"
       << "    <serialNumber>12345</serialNumber>\n"
       << "    <UPC></UPC>\n"
       << "    <UDN>uuid:" << deviceUUID << "</UDN>\n"
       << "    <dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMS-1.50</dlna:X_DLNADOC>\n"
       << "    <presentationURL>" << presentationUrl << "</presentationURL>\n"
       << "    <iconList>\n"
       << "      <icon><mimetype>image/png</mimetype><width>48</width><height>48</height><depth>24</depth><url>/icons/server_icon_48.png</url></icon>\n"
       << "      <icon><mimetype>image/png</mimetype><width>120</width><height>120</height><depth>24</depth><url>/icons/server_icon_120.png</url></icon>\n"
       << "      <icon><mimetype>image/png</mimetype><width>256</width><height>256</height><depth>24</depth><url>/icons/server_icon_256.png</url></icon>\n"
       << "    </iconList>\n"
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
           "<action><name>Search</name><argumentList>\n"
           "<argument><name>ContainerID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument>\n"
           "<argument><name>SearchCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SearchCriteria</relatedStateVariable></argument>\n"
           "<argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument>\n"
           "<argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument>\n"
           "<argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
           "<argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument>\n"
           "<argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument>\n"
           "<argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
           "<argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
           "<argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument>\n"
           "</argumentList></action>\n"
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
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SearchCriteria</name><dataType>string</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_UpdateID</name><dataType>ui4</dataType></stateVariable>\n"
           "<stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>\n"
           "</serviceStateTable>\n</scpd>";
}

std::string ContentDirectory::GetConnectionManagerXML() {
    return R"xml(<?xml version="1.0"?>
<scpd xmlns="urn:schemas-upnp-org:service-1-0">
  <specVersion><major>1</major><minor>0</minor></specVersion>
  <actionList>
    <action>
      <name>GetProtocolInfo</name>
      <argumentList>
        <argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>
        <argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>
      </argumentList>
    </action>
    <action>
      <name>GetCurrentConnectionIDs</name>
      <argumentList>
        <argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument>
      </argumentList>
    </action>
    <action>
      <name>GetCurrentConnectionInfo</name>
      <argumentList>
        <argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>
        <argument><name>RcsID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>
        <argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>
        <argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>
        <argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>
        <argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>
        <argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>
        <argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>
      </argumentList>
    </action>
  </actionList>
  <serviceStateTable>
    <stateVariable sendEvents="yes"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="yes"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="yes"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no">
      <name>A_ARG_TYPE_ConnectionStatus</name>
      <dataType>string</dataType>
      <allowedValueList>
        <allowedValue>OK</allowedValue>
        <allowedValue>ContentFormatMismatch</allowedValue>
        <allowedValue>InsufficientBandwidth</allowedValue>
        <allowedValue>UnreliableChannel</allowedValue>
        <allowedValue>Unknown</allowedValue>
      </allowedValueList>
    </stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no">
      <name>A_ARG_TYPE_Direction</name>
      <dataType>string</dataType>
      <allowedValueList>
        <allowedValue>Input</allowedValue>
        <allowedValue>Output</allowedValue>
      </allowedValueList>
    </stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable>
    <stateVariable sendEvents="no"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable>
  </serviceStateTable>
</scpd>)xml";
}

std::string ContentDirectory::HandleConnectionManagerControl(const std::string& req) {
    bool malformed = false;
    const std::string action = ExtractSoapActionName(req, malformed);
    if (malformed) {
        return SoapFault(400, "Bad Request");
    }

    if (action == "GetProtocolInfo") {
        std::stringstream ss;
        ss << "    <u:GetProtocolInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
           << "<Source>" << XMLEscapeUtf8(BuildSourceProtocolInfoList()) << "</Source>"
           << "<Sink></Sink>"
           << "</u:GetProtocolInfoResponse>";
        return SoapEnvelope(ss.str());
    }

    if (action == "GetCurrentConnectionIDs") {
        return SoapEnvelope("    <u:GetCurrentConnectionIDsResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\"><ConnectionIDs>0</ConnectionIDs></u:GetCurrentConnectionIDsResponse>");
    }

    if (action == "GetCurrentConnectionInfo") {
        std::string idText;
        if (!ExtractTagValue(req, "ConnectionID", idText)) {
            return SoapFault(402, "Invalid Args");
        }
        int connectionId = 0;
        if (!TryParseIntStrict(idText, connectionId) || connectionId != 0) {
            return SoapFault(706, "No such connection");
        }
        std::stringstream ss;
        ss << "    <u:GetCurrentConnectionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
           << "<RcsID>-1</RcsID><AVTransportID>-1</AVTransportID><ProtocolInfo></ProtocolInfo>"
           << "<PeerConnectionManager></PeerConnectionManager><PeerConnectionID>-1</PeerConnectionID>"
           << "<Direction>Output</Direction><Status>OK</Status>"
           << "</u:GetCurrentConnectionInfoResponse>";
        return SoapEnvelope(ss.str());
    }

    return SoapFault(401, "Invalid Action");
}

std::string ContentDirectory::HandleContentDirectoryControl(const std::string& req, const std::string& hostUrl) {
    bool malformed = false;
    const std::string action = ExtractSoapActionName(req, malformed);
    if (malformed) {
        return SoapFault(400, "Bad Request");
    }

    if (action == "GetSystemUpdateID") {
        std::stringstream ss;
        ss << "    <u:GetSystemUpdateIDResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\n"
           << "      <Id>" << AppMedia.GetSystemUpdateID() << "</Id>\n"
           << "    </u:GetSystemUpdateIDResponse>";
       return SoapEnvelope(ss.str());
    }

    if (action == "GetSearchCapabilities") {
        return SoapEnvelope("    <u:GetSearchCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SearchCaps>dc:title,upnp:class</SearchCaps></u:GetSearchCapabilitiesResponse>");
    }

    if (action == "GetSortCapabilities") {
        return SoapEnvelope("    <u:GetSortCapabilitiesResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><SortCaps>dc:title,upnp:class</SortCaps></u:GetSortCapabilitiesResponse>");
    }

    if (action == "Search") {
        if (!DLNAServer.IsInitialScanComplete()) {
            // No content index exists yet at all (Start() has not reached
            // ResetForRescan()). This is the only case 710 should represent --
            // an in-progress-but-partially-populated index is a valid Browse
            // target, not an error (see remediation workflow S1).
            return SoapFault(710, "Content directory not yet initialized");
        }
        std::string containerIdStr;
        std::string searchCriteria;
        std::string filter;
        std::string startingIndexStr;
        std::string requestedCountStr;
        if (!ExtractTagValue(req, "ContainerID", containerIdStr) ||
            !ExtractTagValue(req, "SearchCriteria", searchCriteria) ||
            !ExtractTagValue(req, "StartingIndex", startingIndexStr) ||
            !ExtractTagValue(req, "RequestedCount", requestedCountStr)) {
            return SoapFault(402, "Invalid Args");
        }
        ExtractTagValue(req, "Filter", filter);

        int containerId = 0;
        int startingIndex = 0;
        int requestedCount = 0;
        if (!TryParseIntStrict(containerIdStr, containerId) ||
            !TryParseIntStrict(startingIndexStr, startingIndex) ||
            !TryParseIntStrict(requestedCountStr, requestedCount) ||
            startingIndex < 0 ||
            requestedCount < 0) {
            return SoapFault(402, "Invalid Args");
        }
        if (AppMedia.GetItem(containerId).id == -1) return SoapFault(701, "No such object");

        std::vector<MediaItem> descendants = AppMedia.GetDescendants(containerId);
        std::vector<MediaItem> results;
        for (const auto& item : descendants) {
            if (MatchesSearchCriteria(item, searchCriteria)) {
                results.push_back(item);
            }
        }
        std::string sortCriteria;
        ExtractTagValue(req, "SortCriteria", sortCriteria);
        SortItems(results, sortCriteria);
        return BrowseSearchResponse("Search", results, startingIndex, requestedCount, hostUrl, filter, static_cast<int>(results.size()));
    }

    if (action != "Browse") {
        return SoapFault(401, "Invalid Action");
    }

    if (!DLNAServer.IsInitialScanComplete()) {
        // No content index exists yet at all (Start() has not reached
        // ResetForRescan()). This is the only case 710 should represent --
        // an in-progress-but-partially-populated index is a valid Browse
        // target, not an error (see remediation workflow S1).
        return SoapFault(710, "Content directory not yet initialized");
    }

    std::string objIdStr;
    std::string browseFlag;
    std::string filter;
    std::string startingIndexStr;
    std::string requestedCountStr;
    if (!ExtractTagValue(req, "ObjectID", objIdStr) ||
        !ExtractTagValue(req, "BrowseFlag", browseFlag)) {
        return SoapFault(401, "Invalid XML");
    }
    if (!ExtractTagValue(req, "StartingIndex", startingIndexStr) ||
        !ExtractTagValue(req, "RequestedCount", requestedCountStr)) {
        return SoapFault(402, "Invalid Args");
    }
    ExtractTagValue(req, "Filter", filter);

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
        std::vector<MediaItem> childrenResult;
        auto status = AppMedia.TryGetChildren(objId, childrenResult);
        if (status == MediaSources::GetChildrenResult::NotFound) {
            return SoapFault(701, "No such object");
        } else if (status == MediaSources::GetChildrenResult::NotAContainer) {
            return SoapFault(706, "Not a container");
        }
        results = std::move(childrenResult);
    } else {
        return SoapFault(402, "Invalid Args");
    }

    std::string sortCriteria;
    ExtractTagValue(req, "SortCriteria", sortCriteria);
    SortItems(results, sortCriteria);
    int totalMatches = browseFlag == "BrowseMetadata" ? 1 : static_cast<int>(results.size());
    return BrowseSearchResponse("Browse", results, startingIndex, requestedCount, hostUrl, filter, totalMatches);
}
