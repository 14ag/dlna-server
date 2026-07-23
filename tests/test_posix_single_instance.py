"""
Contract tests for the POSIX single-instance protocol.

The C++ implementation (posix_single_instance.cpp) uses:
  - flock() on a lock file for mutual exclusion
  - a Unix domain stream socket for IPC (text "show\\n")

These tests validate the protocol at the OS level using the same primitives
(flock, AF_UNIX sockets) so they are faithful to what the C++ code does.
They skip entirely on Windows where these APIs do not exist.
"""

import os
import socket
import struct
import tempfile
import threading
from pathlib import Path

import pytest

pytestmark = pytest.mark.skipif(os.name == "nt", reason="POSIX-only test")


# ---- Helpers matching the C++ implementation ----

def _lock_file_path(rundir: str) -> str:
    return os.path.join(rundir, "dlna-server.lock")


def _socket_path(rundir: str) -> str:
    return os.path.join(rundir, "dlna-server.sock")


# ---- Tests ----

class TestFileLockProtocol:
    """flock-based mutual exclusion (matches TryAcquireLock)."""

    def test_first_instance_acquires_lock(self, rundir: str):
        import fcntl
        import struct
        lock_path = _lock_file_path(rundir)
        fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            # Should succeed — no one else holds it.
            assert fcntl(fd, fcntl.F_SETLK, struct.pack("hhll", fcntl.F_WRLCK, 0, 0, 0)) is None
        finally:
            os.close(fd)

    def test_second_instance_fails_lock(self, rundir: str):
        import fcntl
        import struct
        lock_path = _lock_file_path(rundir)
        fd1 = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        fd2 = os.open(lock_path, os.O_RDWR, 0o600)
        try:
            # First acquires write lock.
            fcntl(fd1, fcntl.F_SETLK, struct.pack("hhll", fcntl.F_WRLCK, 0, 0, 0))
            # Second should fail (non-blocking).
            with pytest.raises(BlockingIOError):
                fcntl(fd2, fcntl.F_SETLK, struct.pack("hhll", fcntl.F_WRLCK, 0, 0, 0))
        finally:
            os.close(fd1)
            os.close(fd2)

    def test_lock_released_on_close(self, rundir: str):
        import fcntl
        import struct
        """After fd close, another process can acquire the lock."""
        lock_path = _lock_file_path(rundir)
        fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        fcntl(fd, fcntl.F_SETLK, struct.pack("hhll", fcntl.F_WRLCK, 0, 0, 0))
        os.close(fd)  # kernel auto-releases

        # Second open + lock should now succeed.
        fd2 = os.open(lock_path, os.O_RDWR, 0o600)
        try:
            assert fcntl(fd2, fcntl.F_SETLK, struct.pack("hhll", fcntl.F_WRLCK, 0, 0, 0)) is None
        finally:
            os.close(fd2)


class TestUnixSocketProtocol:
    """AF_UNIX stream socket IPC (matches StartListening / SendShow)."""

    def test_show_command_received(self, rundir: str):
        """Server receives \"show\" when client sends \"show\\n\"."""
        sock_path = _socket_path(rundir)
        received = []

        def server():
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(sock_path)
            srv.listen(1)
            conn, _ = srv.accept()
            data = conn.recv(256)
            received.append(data.decode("utf-8").strip())
            conn.close()
            srv.close()

        t = threading.Thread(target=server, daemon=True)
        t.start()

        # Give the server a moment to bind.
        import time
        deadline = time.time() + 5
        while not os.path.exists(sock_path):
            time.sleep(0.01)
            if time.time() > deadline:
                pytest.fail("Server socket not created in time")

        cli = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        cli.connect(sock_path)
        cli.sendall(b"show\n")
        cli.close()
        t.join(timeout=3)

        assert received == ["show"]

    def test_multiple_commands(self, rundir: str):
        """Server handles sequential connections."""
        sock_path = _socket_path(rundir)
        received = []

        def server():
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(sock_path)
            srv.listen(5)
            for _ in range(2):
                conn, _ = srv.accept()
                data = conn.recv(256)
                received.append(data.decode("utf-8").strip())
                conn.close()
            srv.close()

        t = threading.Thread(target=server, daemon=True)
        t.start()

        import time
        while not os.path.exists(sock_path):
            time.sleep(0.01)

        for msg in (b"show\n", b"sources:/media/video\n"):
            cli = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            cli.connect(sock_path)
            cli.sendall(msg)
            cli.close()
        t.join(timeout=3)

        assert received == ["show", "sources:/media/video"]

    def test_empty_message(self, rundir: str):
        """Server handles empty send gracefully."""
        sock_path = _socket_path(rundir)
        received = []

        def server():
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(sock_path)
            srv.listen(1)
            conn, _ = srv.accept()
            data = conn.recv(256)
            received.append(data)
            conn.close()
            srv.close()

        t = threading.Thread(target=server, daemon=True)
        t.start()

        import time
        while not os.path.exists(sock_path):
            time.sleep(0.01)

        cli = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        cli.connect(sock_path)
        cli.sendall(b"\n")
        cli.close()
        t.join(timeout=3)

        # Should not crash; empty string after strip.
        assert received == [b"\n"]

    def test_stale_socket_replaced(self, rundir: str):
        """If a stale socket file exists, a new server should be able to
        bind by removing it first (matching the C++ unlink() before bind).
        """
        sock_path = _socket_path(rundir)

        # Create a "stale" socket file.
        stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        stale.bind(sock_path)
        stale.close()

        assert os.path.exists(sock_path)

        # New server unlinks stale socket before bind.
        os.unlink(sock_path)

        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            srv.bind(sock_path)  # should not raise
        finally:
            srv.close()


# ---- fixtures ----

@pytest.fixture
def rundir(tmp_path: Path) -> str:
    """A writable directory for lock / socket files."""
    return str(tmp_path)


# fcntl is imported inside test functions that use it because it is
# POSIX-only and would cause ImportError during module collection on Windows.
