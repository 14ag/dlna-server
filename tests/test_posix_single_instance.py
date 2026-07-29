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
import subprocess
import tempfile
import threading
import sys
import time
from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only


# ---- Helpers matching the C++ implementation ----

def _lock_file_path(rundir: str) -> str:
    return os.path.join(rundir, "dlna-server.lock")


def _socket_path(rundir: str) -> str:
    return os.path.join(rundir, "dlna-server.sock")


# ---- Tests ----

class TestFileLockProtocol:
    """flock-based mutual exclusion (matches TryAcquireLock)."""

    def _spawn_lock_holder(self, lock_path: str):
        script = (
            "import fcntl, sys, time\n"
            "path = sys.argv[1]\n"
            "with open(path, 'a+') as f:\n"
            "    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)\n"
            "    print('locked', flush=True)\n"
            "    time.sleep(60)\n"
        )
        proc = subprocess.Popen(
            [sys.executable, "-c", script, lock_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        assert proc.stdout is not None
        assert proc.stdout.readline().strip() == "locked"
        return proc

    def test_first_instance_acquires_lock(self, rundir: str):
        import fcntl
        lock_path = _lock_file_path(rundir)
        fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(fd)

    def test_second_instance_fails_lock(self, rundir: str):
        import fcntl
        lock_path = _lock_file_path(rundir)
        proc = self._spawn_lock_holder(lock_path)
        fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            with pytest.raises(BlockingIOError):
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(fd)
            proc.terminate()
            proc.wait(timeout=5)

    def test_lock_released_on_close(self, rundir: str):
        import fcntl
        """After fd close, another process can acquire the lock."""
        lock_path = _lock_file_path(rundir)
        proc = self._spawn_lock_holder(lock_path)
        proc.terminate()
        proc.wait(timeout=5)
        fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            os.close(fd)


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
        while True:
            try:
                cli.connect(sock_path)
                break
            except ConnectionRefusedError:
                if time.time() > deadline:
                    raise
                time.sleep(0.01)
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
