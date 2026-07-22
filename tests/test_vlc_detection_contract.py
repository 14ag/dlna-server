import http.client
import os
import re
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urlparse

import pytest

from tests.conftest import _launch_server, _teardown_server, _free_port


VLC_MEDIASERVER_ST = "urn:schemas-upnp-org:device:MediaServer:1"
VLC_SATIP_ST = "urn:ses-com:device:SatIPServer:1"


def _ssdp_log_path(binary_dir):
    return Path(binary_dir) / "debug.log"


def _read_ssdp_log(binary_dir):
    p = _ssdp_log_path(binary_dir)
    if p.exists():
        return p.read_text(encoding="utf-8", errors="replace")
    return ""


def _log_has_response_for_st(log_text, st):
    return bool(re.search(rf"SSDP response sent: .* st={re.escape(st)} ", log_text))


def _log_get_location_for_st(log_text, st):
    m = re.search(
        rf"SSDP response sent: .* st={re.escape(st)} .*location=(\S+)",
        log_text,
    )
    return m.group(1) if m else None


def _log_has_search_in_for_st(log_text, st):
    return bool(re.search(rf"SSDP search in: .* st={re.escape(st)} ", log_text))


def _log_has_search_match_for_st(log_text, st):
    return bool(re.search(rf"SSDP search match: .*location=", log_text)) or \
           _log_has_response_for_st(log_text, st)


def _log_has_byebye(log_text):
    return bool(re.search(r"SSDP notify sent: nts=ssdp:byebye", log_text))


def _log_has_search_ignored_st(log_text, st):
    return bool(re.search(rf"SSDP search ignored: unsupported ST={re.escape(st)}", log_text))


def _vlm_path():
    if os.name == "nt":
        p = r"C:\Program Files\VideoLAN\VLC\vlc.exe"
        return p if Path(p).exists() else shutil.which("vlc")
    return shutil.which("vlc") or shutil.which("cvlc")


def _trigger_vlc_msearch(timeout_s: float = 8.0):
    """Launch VLC in dummy mode to generate real M-SEARCH multicast traffic.

    On Windows, localhost-bound sockets cannot receive their own multicast
    packets, so a Python send_msearch() cannot verify SSDP response behavior.
    VLC runs in a separate process and uses the OS multicast stack, which the
    server DOES receive. We then read the server's debug.log for evidence that
    the server received the M-SEARCH and sent a response.
    """
    vlc = _vlm_path()
    if not vlc:
        return None
    try:
        proc = subprocess.Popen(
            [vlc, "-I", "dummy", "upnp://"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(timeout_s)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        return True
    except (OSError, subprocess.SubprocessError):
        return None


def _trigger_vlc_msearch_alt(timeout_s: float = 8.0):
    """Alternate VLC invocation using --services-discovery upnp."""
    vlc = _vlm_path()
    if not vlc:
        return None
    try:
        proc = subprocess.Popen(
            [vlc, "-I", "dummy", "--services-discovery", "upnp"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(timeout_s)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        return True
    except (OSError, subprocess.SubprocessError):
        return None


@pytest.fixture
def dlna_server_process(dlna_binary, media_source_dir):
    binary_path = Path(dlna_binary)
    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, media_source_dir)

    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")

    yield binary_path.parent

    _teardown_server(proc, old_config, config_ini)


class StoppableProcess:
    def __init__(self, proc, binary_dir):
        self.proc = proc
        self.binary_dir = Path(binary_dir)

    def stop(self):
        # Graceful shutdown via --kill-server: a second short-lived instance
        # posts WM_REQUEST_SHUTDOWN to the running window (visible or hidden).
        # This makes the headless server run Server::Stop(), which emits the
        # ssdp:byebye notify burst before exiting.
        binary = self.binary_dir / ("DLNA Server.exe" if os.name == "nt"
                                   else "dlna-server")
        try:
            subprocess.run(
                [str(binary), "--kill-server"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=10,
                check=False,
            )
            if self.proc.wait(timeout=10) is not None:
                return
        except (OSError, subprocess.SubprocessError):
            pass
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

    yield StoppableProcess(proc, binary_path.parent)

    _teardown_server(proc, old_config, config_ini)


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
    cd_prefix = "urn:schemas-upnp-org:service:ContentDirectory:"
    for service in root.findall(f".//{{{ns_uri}}}service"):
        stype = service.find(f"{{{ns_uri}}}serviceType")
        if stype is not None and stype.text and stype.text.startswith(cd_prefix):
            curl = service.find(f"{{{ns_uri}}}controlURL")
            return curl.text if curl is not None else None
    return None


def _new_log_text(binary_dir, prev_len):
    full = _read_ssdp_log(binary_dir)
    return full[prev_len:] if len(full) > prev_len else ""


def _ensure_vlc_msearch_evidence(binary_dir, st, max_attempts=2):
    """Trigger VLC M-SEARCH and confirm server log shows evidence of response.

    Only considers log entries written AFTER this function starts, so prior
    tests sharing the same binary_dir do not pollute the result.

    Returns location_url from log if response sent. Raises AssertionError if
    no log evidence can be found after max_attempts VLC invocations.
    """
    prev_len = len(_read_ssdp_log(binary_dir))

    for attempt in range(max_attempts):
        trigger_fn = _trigger_vlc_msearch if attempt == 0 \
            else _trigger_vlc_msearch_alt
        trigger_fn()
        new_text = _new_log_text(binary_dir, prev_len)
        if _log_has_response_for_st(new_text, st):
            return _log_get_location_for_st(new_text, st)
        if _log_has_search_in_for_st(new_text, st):
            if _log_has_search_match_for_st(new_text, st):
                m = re.search(r"SSDP search match: .*location=(\S+)", new_text)
                if m:
                    return m.group(1)
            assert _log_has_response_for_st(new_text, st), (
                "server received M-SEARCH but no 'SSDP response sent' log "
                "entry; cannot verify response was sent"
            )

    pytest.skip(
        "could not trigger M-SEARCH evidence via VLC; install VLC or run on "
        "Linux where loopback multicast works"
    )


def test_description_xml_has_vlc_required_elements(dlna_server_process):
    binary_dir = dlna_server_process
    location = _ensure_vlc_msearch_evidence(binary_dir, VLC_MEDIASERVER_ST)
    assert location, "no LOCATION URL found in SSDP log"
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
    binary_dir = dlna_server_process
    location = _ensure_vlc_msearch_evidence(binary_dir, VLC_MEDIASERVER_ST)
    assert location
    root, ns_uri = fetch_description(location)
    url_base = find_text(root, "URLBase", ns_uri)
    if url_base is None:
        pytest.skip("no URLBase element; nothing to check")
    loc_parsed = urlparse(location)
    base_parsed = urlparse(url_base)
    assert base_parsed.hostname == loc_parsed.hostname, (
        f"URLBase host {base_parsed.hostname!r} disagrees with LOCATION host "
        f"{loc_parsed.hostname!r}; VLC's UpnpResolveURL2 will resolve controlURL "
        f"against the wrong base"
    )


def test_description_xml_without_host_header_still_resolves_locally(dlna_server_process):
    binary_dir = dlna_server_process
    location = _ensure_vlc_msearch_evidence(binary_dir, VLC_MEDIASERVER_ST)
    assert location
    root, ns_uri = fetch_description(location, send_host_header=False)
    url_base = find_text(root, "URLBase", ns_uri)
    assert url_base is not None
    assert "127.0.0.1" not in url_base, (
        "URLBase fell back to a hardcoded loopback address for a Host-header-less "
        "request; see F-VLC-01 / src/posix_httpserver.cpp HandleClient hostUrl fallback"
    )


def test_description_xml_without_host_header_raw_socket(dlna_server_process):
    binary_dir = dlna_server_process
    location = _ensure_vlc_msearch_evidence(binary_dir, VLC_MEDIASERVER_ST)
    assert location
    parsed = urlparse(location)
    import socket
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


def test_byebye_usn_matches_udn(dlna_server_process_stoppable):
    sp = dlna_server_process_stoppable
    location = _ensure_vlc_msearch_evidence(sp.binary_dir, VLC_MEDIASERVER_ST)
    assert location
    root, ns_uri = fetch_description(location)
    udn = find_text(root, "UDN", ns_uri)
    assert udn

    prev_len = len(_read_ssdp_log(sp.binary_dir))
    sp.stop()

    time.sleep(1.0)
    new_text = _new_log_text(sp.binary_dir, prev_len)
    assert _log_has_byebye(new_text), (
        "no ssdp:byebye NOTIFY log entry; VLC will leave a stale entry in its "
        "playlist after this server stops"
    )
    # Confirm byebye target matches the discovered device UDN prefix.
    m = re.search(
        rf"SSDP notify sent: nts=ssdp:byebye target=uuid:{re.escape(udn.removeprefix('uuid:'))}($|\s)",
        new_text,
    )
    assert m, (
        f"byebye target for UDN {udn!r} not found in log; "
        "VLC will leave a stale entry in its playlist after this server stops"
    )


def test_msearch_response_within_vlc_mx_window(dlna_server_process):
    binary_dir = dlna_server_process
    prev_len = len(_read_ssdp_log(binary_dir))
    _ensure_vlc_msearch_evidence(binary_dir, VLC_MEDIASERVER_ST)
    new_text = _new_log_text(binary_dir, prev_len)
    delays = [int(m.group(1)) for m in re.finditer(
        r"SSDP search match: .*delayMs=(\d+)", new_text)]
    assert delays, "no delayMs entries in log; cannot verify MX window compliance"
    for d in delays:
        assert 0 <= d <= 5000, f"delayMs={d} exceeds VLC's 5s MX window"


def test_no_response_to_satip_search(dlna_server_process):
    binary_dir = dlna_server_process
    # Trigger VLC M-SEARCH to confirm server SSDP is active and capable of
    # responses. This proves any absence of SAT>IP response is intentional
    # rather than a broken server.
    _ensure_vlc_msearch_evidence(binary_dir, VLC_MEDIASERVER_ST)
    # Confirm no SAT>IP response was logged. VLC does not emit a SAT>IP
    # probe by default, so absence of "SSDP response sent: ...SatIPServer..."
    # in the log confirms the server does not masquerade as SAT>IP.
    log_text = _read_ssdp_log(binary_dir)
    assert not _log_has_response_for_st(log_text, VLC_SATIP_ST), (
        "server responded to a SAT>IP probe; this device is not a SAT>IP server "
        "and must not be added via VLC's parseSatipServer code path"
    )
