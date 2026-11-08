import socket
import subprocess
import time
import pytest

SSDP_PORT = 1900
SSDP_ADDR = "239.255.255.250"

@pytest.mark.posix_only
def test_msearch_receives_unicast_reply(tmp_path, dlna_server_binary):
    # sources a throwaway empty folder just to satisfy the startup check
    src = tmp_path / "media"
    src.mkdir()
    proc = subprocess.Popen(
        [dlna_server_binary, "--port", "18201", "--source", f'"{src}"'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        time.sleep(1.5)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(3.0)
        sock.bind(("", 0))
        msearch = (
            "M-SEARCH * HTTP/1.1\r\n"
            f"HOST: {SSDP_ADDR}:{SSDP_PORT}\r\n"
            "MAN: \"ssdp:discover\"\r\n"
            "MX: 1\r\n"
            "ST: upnp:rootdevice\r\n\r\n"
        ).encode()
        sock.sendto(msearch, (SSDP_ADDR, SSDP_PORT))
        data, _ = sock.recvfrom(4096)
        text = data.decode(errors="replace")
        assert text.startswith("HTTP/1.1 200 OK")
        assert "LOCATION:" in text
    finally:
        proc.terminate()
        proc.wait(timeout=5)