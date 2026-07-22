import xml.etree.ElementTree as ET


SOAP_NS = "http://schemas.xmlsoap.org/soap/envelope/"
CD_NS = "urn:schemas-upnp-org:service:ContentDirectory:1"
UPNP_ERR_NS = "urn:schemas-upnp-org:control-1-0"


def build_browse_envelope(object_id="0", browse_flag="BrowseDirectChildren",
                          filter="*", starting_index=0, requested_count=100):
    return (
        f'<?xml version="1.0"?>\n'
        f'<s:Envelope xmlns:s="{SOAP_NS}" '
        f's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">\n'
        f'  <s:Body>\n'
        f'    <u:Browse xmlns:u="{CD_NS}">\n'
        f'      <ObjectID>{object_id}</ObjectID>\n'
        f'      <BrowseFlag>{browse_flag}</BrowseFlag>\n'
        f'      <Filter>{filter}</Filter>\n'
        f'      <StartingIndex>{starting_index}</StartingIndex>\n'
        f'      <RequestedCount>{requested_count}</RequestedCount>\n'
        f'      <SortCriteria></SortCriteria>\n'
        f'    </u:Browse>\n'
        f'  </s:Body>\n'
        f'</s:Envelope>\n'
    )


def build_search_envelope(container_id="0", search_criteria="",
                          filter="*", starting_index=0, requested_count=100):
    return (
        f'<?xml version="1.0"?>\n'
        f'<s:Envelope xmlns:s="{SOAP_NS}" '
        f's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">\n'
        f'  <s:Body>\n'
        f'    <u:Search xmlns:u="{CD_NS}">\n'
        f'      <ContainerID>{container_id}</ContainerID>\n'
        f'      <SearchCriteria>{search_criteria}</SearchCriteria>\n'
        f'      <Filter>{filter}</Filter>\n'
        f'      <StartingIndex>{starting_index}</StartingIndex>\n'
        f'      <RequestedCount>{requested_count}</RequestedCount>\n'
        f'      <SortCriteria></SortCriteria>\n'
        f'    </u:Search>\n'
        f'  </s:Body>\n'
        f'</s:Envelope>\n'
    )


def build_system_update_id_envelope():
    return (
        f'<?xml version="1.0"?>\n'
        f'<s:Envelope xmlns:s="{SOAP_NS}" '
        f's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">\n'
        f'  <s:Body>\n'
        f'    <u:GetSystemUpdateID xmlns:u="{CD_NS}"/>\n'
        f'  </s:Body>\n'
        f'</s:Envelope>\n'
    )


def _ns(tag):
    if tag.startswith("{"):
        return tag
    return f"{{{SOAP_NS}}}{tag}"


def _cdn(tag):
    return f"{{{CD_NS}}}{tag}"


def parse_browse_response(xml_text):
    root = ET.fromstring(xml_text)
    body = root.find(_ns("Body"))
    resp = body.find(_cdn("BrowseResponse"))
    if resp is None:
        # Search and Browse share the same response schema (Result,
        # NumberReturned, TotalMatches, UpdateID).  The server generates
        # both through the same BrowseSearchResponse template, then wraps
        # with either <u:BrowseResponse> or <u:SearchResponse>.
        resp = body.find(_cdn("SearchResponse"))
    if resp is None:
        fault = body.find(_ns("Fault"))
        if fault is not None:
            detail = fault.find("detail")
            upnp_err = None
            if detail is not None:
                upnp_err = detail.find(f"{{{UPNP_ERR_NS}}}UPnPError")
            error_code = -1
            error_desc = ""
            if upnp_err is not None:
                ec = upnp_err.find(f"{{{UPNP_ERR_NS}}}errorCode")
                ed = upnp_err.find(f"{{{UPNP_ERR_NS}}}errorDescription")
                if ec is not None:
                    error_code = int(ec.text)
                if ed is not None:
                    error_desc = ed.text or ""
            return {"NumberReturned": 0, "TotalMatches": 0, "UpdateID": 0,
                    "Result": "", "errorCode": error_code, "errorDescription": error_desc}
        return {"NumberReturned": 0, "TotalMatches": 0, "UpdateID": 0,
                "Result": "", "errorCode": -1, "errorDescription": "unknown"}
    result_el = resp.find("Result")
    num_el = resp.find("NumberReturned")
    total_el = resp.find("TotalMatches")
    uid_el = resp.find("UpdateID")
    import html as _html
    raw = _html.unescape(result_el.text or "") if result_el is not None else ""
    return {
        "Result": raw,
        "NumberReturned": int(num_el.text) if num_el is not None else 0,
        "TotalMatches": int(total_el.text) if total_el is not None else 0,
        "UpdateID": int(uid_el.text) if uid_el is not None else 0,
        "errorCode": 0,
    }


def parse_system_update_id_response(xml_text):
    root = ET.fromstring(xml_text)
    body = root.find(_ns("Body"))
    resp = body.find(_cdn("GetSystemUpdateIDResponse"))
    if resp is None:
        return -1
    uid_el = resp.find("Id")
    if uid_el is None:
        return -1
    return int(uid_el.text)
