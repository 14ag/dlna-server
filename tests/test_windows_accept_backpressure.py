import socket
import time

import pytest

from tests.conftest import _free_port, _launch_server, _teardown_server

pytestmark = pytest.mark.windows_only

MAX_CLIENT_THREADS = 64


def _connect_and_hold(host, port):
    s = socket.create_connection((host, port), timeout=5)
    return s


def test_capacity_connection_queues_instead_of_503(dlna_binary, media_source_dir):
    port = _free_port()
    proc, connected, old_config, config_ini = _launch_server(
        dlna_binary, port, media_source_dir)
    if not connected:
        pytest.fail("Server did not start")
    try:
        held_sockets = [_connect_and_hold("127.0.0.1", port) for _ in range(MAX_CLIENT_THREADS)]
        try:
            overflow = socket.create_connection(("127.0.0.1", port), timeout=5)
            overflow.settimeout(2)
            # before this fix this recv would return a full 503 response
            # almost immediately after this fix it must time out because
            # the accept loop is blocked waiting for capacity
            with pytest.raises(socket.timeout):
                overflow.recv(4096)

            held_sockets[0].close()
            held_sockets = held_sockets[1:]
            overflow.sendall(b"GET /description.xml HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
            overflow.settimeout(10)
            response = overflow.recv(4096)
            assert response.startswith(b"HTTP/1.1 200")
            overflow.close()
        finally:
            for s in held_sockets:
                s.close()
    finally:
        _teardown_server(proc, old_config, config_ini)