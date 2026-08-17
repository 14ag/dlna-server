import http.client
import os
import re
import socket
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urlparse

import pytest

from tests.conftest import _launch_server, _teardown_server, _free_port


VLC_MEDIASERVER_ST = "urn:schemas-upnp-org:device:MediaServer:1"
VLC_SATIP_ST = "urn:ses-com:device:SatIPServer:1"


class ServerSession:
    """Bundles the running-server directories this module's helpers need.

    On Windows the binary, config.ini and debug.log all live in one directory,
    so `binary_dir == config_dir` and `log_dir == config_dir/debug.log`. On a
    POSIX server the debug.log is written next to the *config* file (XDG
    config dir), NOT next to the binary, so a test that reads
    `binary_dir/debug.log` would see nothing and spuriously skip even on a
    healthy Linux host. Carrying both dirs explicitly removes that ambiguity.
    """

    def __init__(self, binary_dir, config_dir):
        self.binary_dir = Path(binary_dir)
        self.config_dir = Path(config_dir)

    @property
    def log_dir(self):
        if os.name == "nt":
            # On Windows _launch_server forces the config.ini next to the
            # binary, and debug.log is derived from the config path, so the
            # log ends up beside the executable. Reading config_dir (the temp
            # media-source parent the fixture passes for symmetry) would see
            # nothing.
            return self.binary_dir
        return self.config_dir / "dlna-server"

    @property
    def binary(self):
        name = "DLNA Server.exe" if os.name == "nt" else "dlna-server"
        return self.binary_dir / name


def _ssdp_log_path(session):
    if isinstance(session, ServerSession):
        return session.log_dir / "debug.log"
    return Path(session) / "debug.log"


def _read_ssdp_log(session):
    p = _ssdp_log_path(session)
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


def _server_bind_ip(session):
    log_text = _read_ssdp_log(session)
    # Windows logs "Starting server on <ip>:<port>"; POSIX logs
    # "DLNA server running on <ip>:<port>". Accept both wordings.
    m = re.search(r"(?:Starting server|DLNA server running) on ([\d.]+):\d+", log_text)
    return m.group(1) if m else None


def _probe_msearch(session, st, count=3):
    """Send real M-SEARCH datagrams on the SSDP group from the interface the
    server bound. The server joins the multicast group on that interface, so
    it receives and logs the search, then logs its response. IP_MULTICAST_IF
    is required on Windows where a loopback socket never delivers its own
    multicast -- the same trick the flood test in this file already uses.
    """
    from tests.fixtures.ssdp_listener import SSDP_ADDR, SSDP_PORT
    server_ip = _server_bind_ip(session)
    if not server_ip:
        return False
    msearch = (
        "M-SEARCH * HTTP/1.1\r\n"
        f"HOST: {SSDP_ADDR}:{SSDP_PORT}\r\n"
        'MAN: "ssdp:discover"\r\n'
        "MX: 3\r\n"
        f"ST: {st}\r\n\r\n"
    ).encode()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                        socket.inet_aton(server_ip))
        for _ in range(count):
            sock.sendto(msearch, (SSDP_ADDR, SSDP_PORT))
    finally:
        sock.close()
    return True


@pytest.fixture
def dlna_server_process(dlna_binary, media_source_dir):
    binary_path = Path(dlna_binary)
    port = _free_port()
    config_dir = media_source_dir.parent
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, media_source_dir, config_dir=config_dir)

    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")

    yield ServerSession(binary_path.parent, config_dir)

    _teardown_server(proc, old_config, config_ini)


class StoppableProcess:
    def __init__(self, proc, binary_dir, config_dir):
        self.proc = proc
        self.binary_dir = Path(binary_dir)
        self.config_dir = Path(config_dir)
        self._session = ServerSession(binary_dir, config_dir)

    def stop(self):
        # Graceful shutdown via --kill-server: a second short-lived instance
        # posts WM_REQUEST_SHUTDOWN to the running window (visible or hidden).
        # This makes the headless server run Server::Stop(), which emits the
        # ssdp:byebye notify burst before exiting.
        binary = self._session.binary
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
    config_dir = media_source_dir.parent
    proc, connected, old_config, config_ini = _launch_server(
        binary_path, port, media_source_dir, config_dir=config_dir)

    if not connected:
        _teardown_server(proc, old_config, config_ini)
        pytest.fail(f"Server did not listen on port {port} within 15s")

    yield StoppableProcess(proc, binary_path.parent, config_dir)

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


def _new_log_text(session, prev_len):
    full = _read_ssdp_log(session)
    return full[prev_len:] if len(full) > prev_len else ""


def _ensure_vlc_msearch_evidence(session, st, max_attempts=2):
    """Probe M-SEARCH and confirm server log shows evidence of a response.

    Only considers log entries written AFTER this function starts, so prior
    tests sharing the same binary_dir do not pollute the result.

    Returns location_url from log if response sent. Raises AssertionError if
    no log evidence can be found after max_attempts probes.
    """
    prev_len = len(_read_ssdp_log(session))

    for _ in range(max_attempts):
        if not _probe_msearch(session, st):
            break
        time.sleep(1.5)  # let the server log the match and send response
        new_text = _new_log_text(session, prev_len)
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

    raise AssertionError(
        f"could not obtain M-SEARCH response evidence for {st}; "
        "multicast probe failed"
    )


def test_description_xml_has_vlc_required_elements(dlna_server_process):
    session = dlna_server_process
    location = _ensure_vlc_msearch_evidence(session, VLC_MEDIASERVER_ST)
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
    session = dlna_server_process
    location = _ensure_vlc_msearch_evidence(session, VLC_MEDIASERVER_ST)
    assert location
    root, ns_uri = fetch_description(location)
    url_base = find_text(root, "URLBase", ns_uri)
    assert url_base is not None, "URLBase element missing from description.xml"
    loc_parsed = urlparse(location)
    base_parsed = urlparse(url_base)
    assert base_parsed.hostname == loc_parsed.hostname, (
        f"URLBase host {base_parsed.hostname!r} disagrees with LOCATION host "
        f"{loc_parsed.hostname!r}; VLC's UpnpResolveURL2 will resolve controlURL "
        f"against the wrong base"
    )


def test_description_xml_without_host_header_still_resolves_locally(dlna_server_process):
    session = dlna_server_process
    location = _ensure_vlc_msearch_evidence(session, VLC_MEDIASERVER_ST)
    assert location
    root, ns_uri = fetch_description(location, send_host_header=False)
    url_base = find_text(root, "URLBase", ns_uri)
    assert url_base is not None
    assert "127.0.0.1" not in url_base, (
        "URLBase fell back to a hardcoded loopback address for a Host-header-less "
        "request; see F-VLC-01 / src/posix_httpserver.cpp HandleClient hostUrl fallback"
    )


def test_description_xml_without_host_header_raw_socket(dlna_server_process):
    session = dlna_server_process
    location = _ensure_vlc_msearch_evidence(session, VLC_MEDIASERVER_ST)
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
    location = _ensure_vlc_msearch_evidence(sp._session, VLC_MEDIASERVER_ST)
    assert location
    root, ns_uri = fetch_description(location)
    udn = find_text(root, "UDN", ns_uri)
    assert udn

    prev_len = len(_read_ssdp_log(sp._session))
    sp.stop()

    time.sleep(1.0)
    new_text = _new_log_text(sp._session, prev_len)
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
    session = dlna_server_process
    prev_len = len(_read_ssdp_log(session))
    _ensure_vlc_msearch_evidence(session, VLC_MEDIASERVER_ST)
    new_text = _new_log_text(session, prev_len)
    delays = [int(m.group(1)) for m in re.finditer(
        r"SSDP search match: .*delayMs=(\d+)", new_text)]
    assert delays, "no delayMs entries in log; cannot verify MX window compliance"
    for d in delays:
        assert 0 <= d <= 5000, f"delayMs={d} exceeds VLC's 5s MX window"


def test_no_response_to_satip_search(dlna_server_process):
    session = dlna_server_process
    # Trigger VLC M-SEARCH to confirm server SSDP is active and capable of
    # responses. This proves any absence of SAT>IP response is intentional
    # rather than a broken server.
    _ensure_vlc_msearch_evidence(session, VLC_MEDIASERVER_ST)
    # Confirm no SAT>IP response was logged. VLC does not emit a SAT>IP
    # probe by default, so absence of "SSDP response sent: ...SatIPServer..."
    # in the log confirms the server does not masquerade as SAT>IP.
    log_text = _read_ssdp_log(session)
    assert not _log_has_response_for_st(log_text, VLC_SATIP_ST), (
        "server responded to a SAT>IP probe; this device is not a SAT>IP server "
        "and must not be added via VLC's parseSatipServer code path"
    )


@pytest.fixture
def ssdp_multicast_addr():
    """(host, port) tuple for the SSDP multicast group, reused by the
    flood test. Constants come from the project's existing ssdp_listener."""
    from tests.fixtures.ssdp_listener import SSDP_ADDR, SSDP_PORT
    return (SSDP_ADDR, SSDP_PORT)


@pytest.mark.windows_only
def test_ssdp_search_flood_does_not_degrade_or_hang(
    dlna_server_process, ssdp_multicast_addr
):
    """F-PERF-01: send more than kMaxDelayedResponses (256) M-SEARCH requests
    in a short window and confirm the sender is not blocked and the server
    keeps answering afterward.

    On Windows a localhost socket cannot receive its own multicast (see the
    module docstring above), so server liveness after the flood is proven by
    reading the server's debug.log instead of recvfrom(). IP_MULTICAST_IF is
    set to the interface the server bound (parsed from its startup log line)
    so the M-SEARCH datagrams actually reach it.
    """
    session = dlna_server_process
    pre_log = _read_ssdp_log(session)
    pre_len = len(pre_log)

    m = re.search(r"Starting server on ([\d.]+):(\d+)", pre_log)
    assert m, "could not parse server bind address from debug.log"
    server_ip = m.group(1)

    msearch = (
        "M-SEARCH * HTTP/1.1\r\n"
        f"HOST: {ssdp_multicast_addr[0]}:{ssdp_multicast_addr[1]}\r\n"
        'MAN: "ssdp:discover"\r\n'
        "MX: 3\r\n"
        "ST: ssdp:all\r\n\r\n"
    ).encode()

    request_count = 400
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Without this the OS picks a default/route that does not deliver loopback
    # multicast to the LAN-bound listener the server joined.
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                    socket.inet_aton(server_ip))

    start = time.monotonic()
    for _ in range(request_count):
        sock.sendto(msearch, ssdp_multicast_addr)
    elapsed_send = time.monotonic() - start
    # Sending 400 short UDP datagrams must not block the sender (this asserts
    # the SENDER isn't starved, not the server's queue -- see note above).
    assert elapsed_send < 5.0, f"flood send took too long: {elapsed_send:.2f}s"

    # The flood must actually reach the server (else the liveness check below
    # would be vacuous). Count newly-logged searches for ssdp:all, polling for
    # the server to drain its receive queue after the burst.
    deadline = time.monotonic() + 5.0
    flood_received = 0
    while time.monotonic() < deadline:
        new_text = _read_ssdp_log(session)[pre_len:]
        flood_received = len(re.findall(r"SSDP search in: .* st=ssdp:all ", new_text))
        if flood_received >= 100:
            break
        time.sleep(0.2)
    assert flood_received >= 100, (
        f"server did not receive the flood (logged {flood_received}/{request_count} "
        "ssdp:all searches); multicast delivery failed"
    )

    # The server must still be alive and answering SSDP after absorbing the
    # flood. The follow-up uses a distinct ST (upnp:rootdevice) so it cannot
    # coalesce with the ssdp:all flood and proves the server keeps processing.
    followup = (
        "M-SEARCH * HTTP/1.1\r\n"
        f"HOST: {ssdp_multicast_addr[0]}:{ssdp_multicast_addr[1]}\r\n"
        'MAN: "ssdp:discover"\r\n'
        "MX: 1\r\n"
        "ST: upnp:rootdevice\r\n\r\n"
    ).encode()
    follow_pre_len = len(_read_ssdp_log(session))
    sock.sendto(followup, ssdp_multicast_addr)
    sock.close()

    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        log = _read_ssdp_log(session)[follow_pre_len:]
        if _log_has_search_in_for_st(log, "upnp:rootdevice"):
            break
        time.sleep(0.2)
    else:
        pytest.fail(
            "server did not process follow-up M-SEARCH after flood; "
            "SSDP handling degraded or hung"
        )
