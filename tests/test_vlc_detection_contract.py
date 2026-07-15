import socket
import http.client
import xml.etree.ElementTree as ET
from urllib.parse import urlparse
import time
import struct
import subprocess
import pytest
import os

if os.name == "nt":
    import ctypes
    if not ctypes.windll.shell32.IsUserAnAdmin():
        pytestmark = pytest.mark.skip("SSDP multicast join requires admin on Windows")

from tests.conftest import _launch_server, _teardown_server, _free_port
from pathlib import Path

SSDP_ADDR = ("239.255.255.250", 1900)
VLC_MEDIASERVER_ST = "urn:schemas-upnp-org:device:MediaServer:1"
VLC_SATIP_ST = "urn:ses-com:device:SatIPServer:1"

@pytest.fixture
def dlna_server_process(dlna_binary, media_source_dir):
    binary_path = Path(dlna_binary)
    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, media_source_dir)
    
    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")
        
    yield proc
    
    _teardown_server(proc, old_config, config_ini)

class StoppableProcess:
    def __init__(self, proc):
        self.proc = proc
    def stop(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)

@pytest.fixture
def dlna_server_process_stoppable(dlna_binary, media_source_dir):
    binary_path = Path(dlna_binary)
    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, media_source_dir)
        
    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")
        
    yield StoppableProcess(proc)
    
    _teardown_server(proc, old_config, config_ini)

def send_msearch(st: str, mx: int = 5, timeout: float = 6.0) -> list[dict]:
    msg = (
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        f"MX: {mx}\r\n"
        f"ST: {st}\r\n\r\n"
    ).encode("ascii")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(timeout)
    sock.sendto(msg, SSDP_ADDR)
    responses = []
    try:
        while True:
            data, _ = sock.recvfrom(8192)
            responses.append(parse_headers(data.decode("utf-8", "replace")))
    except socket.timeout:
        pass
    finally:
        sock.close()
    return responses

def parse_headers(raw: str) -> dict:
    lines = raw.split("\r\n")
    headers = {}
    for line in lines[1:]:
        if ":" not in line:
            continue
        key, _, value = line.partition(":")
        headers[key.strip().upper()] = value.strip()
    return headers

def fetch_description(location_url: str, send_host_header: bool = True) -> tuple[ET.Element, str]:
    parsed = urlparse(location_url)
    conn = http.client.HTTPConnection(parsed.hostname, parsed.port, timeout=5)
    headers = {}
    if send_host_header:
        headers["Host"] = parsed.netloc
    conn.request("GET", parsed.path or "/description.xml", headers=headers)
    resp = conn.getresponse()
    body = resp.read()
    conn.close()
    ns = {"u": "urn:schemas-upnp-org:device-1-0"}
    root = ET.fromstring(body)
    return root, ns["u"]

def find_text(root: ET.Element, tag: str, ns_uri: str) -> str | None:
    el = root.find(f".//{{{ns_uri}}}{tag}")
    return el.text if el is not None else None

def find_content_directory_control_url(root: ET.Element, ns_uri: str) -> str | None:
    for service in root.findall(f".//{{{ns_uri}}}service"):
        stype = service.find(f"{{{ns_uri}}}serviceType")
        if stype is not None and stype.text and stype.text.startswith(
            "urn:schemas-upnp-org:service:ContentDirectory:1"[:-1]
        ):
            curl = service.find(f"{{{ns_uri}}}controlURL")
            return curl.text if curl is not None else None
    return None

def test_description_xml_has_vlc_required_elements(dlna_server_process):
    responses = send_msearch(VLC_MEDIASERVER_ST)
    assert responses, "no SSDP response to VLC's exact MediaServer M-SEARCH"
    location = responses[0]["LOCATION"]
    root, ns_uri = fetch_description(location)

    device_type = find_text(root, "deviceType", ns_uri)
    assert device_type is not None
    assert device_type.startswith(VLC_MEDIASERVER_ST[:-1]), (
        "deviceType must match VLC's version-tolerant prefix check"
    )

    udn = find_text(root, "UDN", ns_uri)
    assert udn and udn.startswith("uuid:"), "UDN missing or malformed; VLC skips devices with no UDN"

    friendly_name = find_text(root, "friendlyName", ns_uri)
    assert friendly_name, "friendlyName missing; VLC skips devices with no friendlyName"

    control_url = find_content_directory_control_url(root, ns_uri)
    assert control_url, (
        "no ContentDirectory service with a controlURL; VLC will discover but never list this device"
    )

def test_urlbase_matches_location_host(dlna_server_process):
    responses = send_msearch(VLC_MEDIASERVER_ST)
    assert responses
    for response in responses:
        location = response["LOCATION"]
        root, ns_uri = fetch_description(location)
        url_base = find_text(root, "URLBase", ns_uri)
        if url_base is None:
            continue
        loc_parsed = urlparse(location)
        base_parsed = urlparse(url_base)
        assert base_parsed.hostname == loc_parsed.hostname, (
            f"URLBase host {base_parsed.hostname!r} disagrees with LOCATION host "
            f"{loc_parsed.hostname!r}; VLC's UpnpResolveURL2 will resolve controlURL "
            f"against the wrong base"
        )

def test_description_xml_without_host_header_still_resolves_locally(dlna_server_process):
    responses = send_msearch(VLC_MEDIASERVER_ST)
    assert responses
    location = responses[0]["LOCATION"]
    root, ns_uri = fetch_description(location, send_host_header=False)
    url_base = find_text(root, "URLBase", ns_uri)
    assert url_base is not None
    assert "127.0.0.1" not in url_base, (
        "URLBase fell back to a hardcoded loopback address for a Host-header-less "
        "request; see F-VLC-01 / src/posix_httpserver.cpp HandleClient hostUrl fallback"
    )

def test_description_xml_without_host_header_raw_socket(dlna_server_process):
    responses = send_msearch(VLC_MEDIASERVER_ST)
    assert responses
    location = responses[0]["LOCATION"]
    parsed = urlparse(location)
    sock = socket.create_connection((parsed.hostname, parsed.port), timeout=5)
    sock.sendall(f"GET {parsed.path} HTTP/1.0\r\n\r\n".encode("ascii"))
    raw = b""
    sock.settimeout(5)
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            raw += chunk
    except socket.timeout:
        pass
    sock.close()
    body = raw.split(b"\r\n\r\n", 1)[1]
    root = ET.fromstring(body)
    ns_uri = "urn:schemas-upnp-org:device-1-0"
    url_base = find_text(root, "URLBase", ns_uri)
    assert url_base is not None
    assert "127.0.0.1" not in url_base

def listen_for_notify(nts_filter: str, timeout: float = 20.0) -> dict | None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, 'SO_REUSEPORT'):
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.bind(("", 1900))
    mreq = struct.pack("4sl", socket.inet_aton("239.255.255.250"), socket.INADDR_ANY)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    sock.settimeout(timeout)
    try:
        while True:
            data, _ = sock.recvfrom(8192)
            text = data.decode("utf-8", "replace")
            headers = parse_headers(text)
            if headers.get("NTS", "").lower() == nts_filter and \
               headers.get("NT", "") == "uuid:" + headers.get("USN", "uuid:").split("::")[0][5:]:
                pass
            if headers.get("NTS", "").lower() == nts_filter:
                return headers
    except socket.timeout:
        return None
    finally:
        sock.close()

def test_byebye_usn_matches_udn(dlna_server_process_stoppable):
    responses = send_msearch(VLC_MEDIASERVER_ST)
    assert responses
    location = responses[0]["LOCATION"]
    root, ns_uri = fetch_description(location)
    udn = find_text(root, "UDN", ns_uri)
    assert udn

    dlna_server_process_stoppable.stop()

    byebye = listen_for_notify("ssdp:byebye", timeout=10.0)
    assert byebye is not None, "no ssdp:byebye NOTIFY observed on shutdown"
    assert byebye.get("NT") == "uuid:" + udn.removeprefix("uuid:"), (
        "byebye NT does not match description.xml UDN; VLC will leave a "
        "stale entry in its playlist after this server stops"
    )
    assert byebye.get("USN") == byebye.get("NT"), (
        "for the bare-uuid SSDP target, USN must equal NT with no '::' suffix"
    )

def test_msearch_response_within_vlc_mx_window(dlna_server_process):
    samples = []
    for _ in range(5):
        start = time.monotonic()
        responses = send_msearch(VLC_MEDIASERVER_ST, mx=5, timeout=6.0)
        elapsed = time.monotonic() - start
        assert responses, "no response to VLC's ST/MX combination"
        samples.append(elapsed)
    assert all(s <= 5.5 for s in samples), (
        f"response(s) exceeded VLC's 5s MX window: {samples}"
    )
    assert max(samples) - min(samples) > 0.01, (
        "response delay looks non-randomized (all samples nearly identical); "
        "verify ComputeDelayMilliseconds in src/ssdp_common.cpp is still applied"
    )

def test_no_response_to_satip_search(dlna_server_process):
    responses = send_msearch(VLC_SATIP_ST, mx=3, timeout=4.0)
    assert not responses, (
        "server responded to VLC's SAT>IP probe; this device is not a SAT>IP server "
        "and must not be added via VLC's parseSatipServer code path"
    )
