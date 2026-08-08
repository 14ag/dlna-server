import socket
import time
import xml.etree.ElementTree as ET

import pytest

from conftest import ServerClient
from tests.conftest import _free_port, _launch_server, _teardown_server

pytestmark = pytest.mark.posix_only

NS = {"d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"}


def _browse_first_media_id(client):
    item_id = None
    deadline = time.time() + 20
    while time.time() < deadline and item_id is None:
        stack = ["0"]
        visited = set()
        while stack and item_id is None:
            cid = stack.pop()
            if cid in visited:
                continue
            visited.add(cid)
            resp = client.soap_browse(cid)
            didl = resp.get("Result", "")
            if not didl:
                continue
            root = ET.fromstring(didl)
            for item in root.findall("d:item", NS):
                item_id = item.get("id")
                break
            if item_id is None:
                for container in root.findall("d:container", NS):
                    child = container.get("id")
                    if child and child not in visited:
                        stack.append(child)
        if item_id is None:
            time.sleep(0.5)
    return item_id


def test_head_on_local_media_does_not_split_response(dlna_binary, media_source_dir):
    (media_source_dir / "a.mp3").write_bytes(b"\x00" * 4096)

    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        dlna_binary, port, media_source_dir)
    if not connected:
        pytest.fail("Server did not start")
    try:
        # discover the media id via Browse ContentDirectory instead of
        # hardcoding one see contentdirectory cpp HandleContentDirectoryControl
        # for the soap action this repo already uses in similar tests
        client = ServerClient(f"http://127.0.0.1:{port}", dlna_binary)
        media_id = _browse_first_media_id(client)
        assert media_id is not None, "No media item found via Browse"

        sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        sock.sendall(
            f"HEAD /media/{media_id}.mp3 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n".encode()
        )
        sock.settimeout(2)
        first_response = sock.recv(65536)
        assert first_response.count(b"HTTP/1.1") == 1, (
            "HEAD response contained more than one HTTP status line: " + repr(first_response)
        )
        assert first_response.startswith(b"HTTP/1.1 200")

        # the connection must still be usable for a second request this
        # is what actually breaks for a real dlna client if the bug is present
        sock.sendall(b"GET /description.xml HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
        sock.settimeout(5)
        second_response = sock.recv(65536)
        assert second_response.startswith(b"HTTP/1.1 200")
        sock.close()
    finally:
        _teardown_server(proc, old_config, config_ini)