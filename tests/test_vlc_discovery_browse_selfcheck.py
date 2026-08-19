"""
Self-check for test_vlc_discovery_browse.py's protocol logic. Runs with NO
running dlna-server and NO network access: it exercises the SSDP
request/response builders and parsers, the Browse SOAP envelope builder,
and the DIDL-Lite parse path against synthetic fixtures shaped exactly
like real dlna-server output (see src/ssdp.cpp SendDelayedSearchResponse
and src/contentdirectory.cpp BuildDIDL for the shapes these fixtures
mirror). If this file fails, the VLC-equivalence test module itself is
broken and its results against a real server cannot be trusted.

All synthetic fixtures use TEST-NET addresses (RFC 5737: 192.0.2.x) and
arbitrary port numbers -- never a real server IP or control server address.
"""

import socket
import xml.etree.ElementTree as ET

from test_vlc_discovery_browse import (
    build_msearch_request,
    parse_ssdp_response,
    build_browse_soap_envelope,
    DIDL_NS,
    CONTENT_DIRECTORY_SOAP_ACTION,
)

# RFC 5737 TEST-NET address -- never routable, never a real server
_SYNTHETIC_HOST = "192.0.2.1"
_SYNTHETIC_PORT = 8200


def test_msearch_request_shape():
    request = build_msearch_request().decode()
    assert request.startswith("M-SEARCH * HTTP/1.1\r\n")
    assert 'MAN: "ssdp:discover"' in request
    assert "ST: urn:schemas-upnp-org:device:MediaServer:1" in request
    assert request.endswith("\r\n\r\n")


def test_ssdp_response_parsing_matches_real_server_output_shape():
    # Shaped exactly like SSDP::HandleSearchRequest's response construction
    # in src/ssdp.cpp / src/posix_ssdp.cpp.
    location = f"http://{_SYNTHETIC_HOST}:{_SYNTHETIC_PORT}/description.xml"
    synthetic_response = (
        "HTTP/1.1 200 OK\r\n"
        "CACHE-CONTROL: max-age=1800\r\n"
        "DATE: Wed, 19 Aug 2026 00:00:00 GMT\r\n"
        "EXT:\r\n"
        f"LOCATION: {location}\r\n"
        "SERVER: Linux/1.0 DLNADOC/1.50 UPnP/1.0 dlna-server/1.7.0\r\n"
        "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "USN: uuid:abc-123::urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "BOOTID.UPNP.ORG: 123\r\n"
        "CONFIGID.UPNP.ORG: 1\r\n\r\n"
    ).encode()
    parsed = parse_ssdp_response(synthetic_response, _SYNTHETIC_HOST)
    assert parsed is not None
    assert parsed.location == location
    assert "DLNADOC/1.50" in parsed.server


def test_ssdp_response_rejects_non_200():
    bad = b"HTTP/1.1 404 Not Found\r\n\r\n"
    assert parse_ssdp_response(bad, _SYNTHETIC_HOST) is None


def test_browse_soap_envelope_well_formed_and_matches_captured_vlc_shape():
    envelope = build_browse_soap_envelope(object_id="0", requested_count=0)
    # Well-formedness: must parse as XML with no error.
    ET.fromstring(envelope)
    expected_fragment = (
        "<ObjectID>0</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter><StartingIndex>0</StartingIndex>"
        "<RequestedCount>0</RequestedCount><SortCriteria></SortCriteria>"
    )
    assert expected_fragment in envelope
    assert CONTENT_DIRECTORY_SOAP_ACTION == '"urn:schemas-upnp-org:service:ContentDirectory:1#Browse"'


def test_didl_parse_path_against_synthetic_builddidl_output():
    # Shaped exactly like ContentDirectory::BuildDIDL's output in
    # src/contentdirectory.cpp: one container with childCount, one item
    # with a res URL. Port is allocated dynamically so this test has no
    # hardcoded address -- only the structural assertions matter.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    res_url = f"http://127.0.0.1:{port}/media/1000001.mp3"
    didl = (
        '<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" '
        'xmlns:dc="http://purl.org/dc/elements/1.1/" '
        'xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">'
        '<container id="1000000" parentID="0" childCount="2" restricted="1">'
        "<dc:title>Movies</dc:title>"
        "<upnp:class>object.container.storageFolder</upnp:class>"
        "</container>"
        '<item id="1000001" parentID="0" restricted="1">'
        "<dc:title>song.mp3</dc:title>"
        "<upnp:class>object.item.audioItem.musicTrack</upnp:class>"
        f'<res protocolInfo="http-get:*:audio/mpeg:*">{res_url}</res>'
        "</item>"
        "</DIDL-Lite>"
    )
    root = ET.fromstring(didl)
    containers = root.findall("didl:container", DIDL_NS)
    items = root.findall("didl:item", DIDL_NS)
    assert len(containers) == 1
    assert containers[0].get("childCount") == "2"
    assert len(items) == 1
    res_el = items[0].find("didl:res", DIDL_NS)
    assert res_el is not None
    assert res_el.text.endswith(".mp3")


def test_browse_soap_call_over_real_socket_against_synthetic_http_server():
    """
    Full round trip over a real TCP socket (not string matching only):
    spins up a minimal stdlib HTTP server that returns dlna-server-shaped
    description.xml and BrowseResponse bodies, then drives the actual
    fetch_device_description()/send_browse() functions against it. This
    is the strongest self-check available without a compiled dlna-server
    binary: it proves the HTTP client code paths (URL parsing, header
    construction, response parsing) work end-to-end over a socket, not
    just against in-memory strings.
    """
    import http.server
    import socketserver
    import threading

    from test_vlc_discovery_browse import fetch_device_description, send_browse

    description_xml = b"""<?xml version="1.0" encoding="utf-8"?>
<root xmlns="urn:schemas-upnp-org:device-1-0">
  <device>
    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
    <friendlyName>Self-Check DLNA Server</friendlyName>
    <UDN>uuid:selfcheck-udn-0000</UDN>
    <serviceList>
      <service>
        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>
        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>
        <SCPDURL>/ContentDirectory.xml</SCPDURL>
        <controlURL>/upnp/control/content_directory</controlURL>
      </service>
    </serviceList>
  </device>
</root>"""

    browse_response = b"""<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
      <Result>&lt;DIDL-Lite xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot; xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot;&gt;&lt;container id=&quot;1000000&quot; parentID=&quot;0&quot; childCount=&quot;1&quot; restricted=&quot;1&quot;&gt;&lt;dc:title&gt;Movies&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;/container&gt;&lt;/DIDL-Lite&gt;</Result>
      <NumberReturned>1</NumberReturned>
      <TotalMatches>1</TotalMatches>
      <UpdateID>1</UpdateID>
    </u:BrowseResponse>
  </s:Body>
</s:Envelope>"""

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            if self.path == "/description.xml":
                self.send_response(200)
                self.send_header("Content-Type", 'text/xml; charset="utf-8"')
                self.send_header("Content-Length", str(len(description_xml)))
                self.end_headers()
                self.wfile.write(description_xml)
            else:
                self.send_response(404)
                self.end_headers()

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length)
            assert self.headers.get("SOAPACTION") == CONTENT_DIRECTORY_SOAP_ACTION
            assert b"<ObjectID>0</ObjectID>" in body
            assert b"<BrowseFlag>BrowseDirectChildren</BrowseFlag>" in body
            self.send_response(200)
            self.send_header("Content-Type", 'text/xml; charset="utf-8"')
            self.send_header("Content-Length", str(len(browse_response)))
            self.end_headers()
            self.wfile.write(browse_response)

    with socketserver.TCPServer(("127.0.0.1", 0), Handler) as httpd:
        port = httpd.server_address[1]
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            desc = fetch_device_description(f"http://127.0.0.1:{port}/description.xml")
            assert desc.friendly_name == "Self-Check DLNA Server"
            assert desc.udn == "uuid:selfcheck-udn-0000"
            assert desc.content_directory_control_url == (
                f"http://127.0.0.1:{port}/upnp/control/content_directory"
            )

            result = send_browse(desc.content_directory_control_url, object_id="0")
            assert result.number_returned == 1
            assert result.total_matches == 1
            assert result.update_id == 1
            assert len(result.items) == 1
            assert result.items[0].is_container
            assert result.items[0].title == "Movies"
            assert result.items[0].child_count == 1
        finally:
            httpd.shutdown()


def test_browse_fault_response_surfaces_as_assertion_error():
    """
    Confirms send_browse() correctly treats a non-200 SOAP fault response
    (the shape src/contentdirectory.cpp's SoapFault(701, ...) produces)
    as a failure, matching how a real control point distinguishes a
    UPnPError fault from a successful BrowseResponse.
    """
    import http.server
    import socketserver
    import threading

    import pytest

    from test_vlc_discovery_browse import send_browse

    fault_body = (
        b'<?xml version="1.0"?>'
        b'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
        b's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">'
        b"<s:Body><s:Fault><faultcode>s:Client</faultcode>"
        b"<faultstring>UPnPError</faultstring><detail>"
        b'<UPnPError xmlns="urn:schemas-upnp-org:control-1-0">'
        b"<errorCode>701</errorCode>"
        b"<errorDescription>No such object</errorDescription>"
        b"</UPnPError></detail></s:Fault></s:Body></s:Envelope>"
    )

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            self.rfile.read(length)
            self.send_response(500)
            self.send_header("Content-Type", "text/xml")
            self.send_header("Content-Length", str(len(fault_body)))
            self.end_headers()
            self.wfile.write(fault_body)

    with socketserver.TCPServer(("127.0.0.1", 0), Handler) as httpd:
        port = httpd.server_address[1]
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            with pytest.raises(AssertionError):
                send_browse(f"http://127.0.0.1:{port}/x", object_id="bad-id")
        finally:
            httpd.shutdown()
