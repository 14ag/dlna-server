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
import shutil
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

def _global_instance_dir() -> str:
    """The fixed per-user instance dir used by the C++ single-instance code
    (posix_single_instance.cpp GetInstanceDir): /tmp/dlna-server-<uid>.

    Uses /tmp directly -- NOT tempfile.gettempdir() -- because conftest.py
    redirects tempfile.tempdir to the repo's tmp/ tree, which may live on a
    drvfs/9p mount where the C++ instance dir is /tmp regardless."""
    return os.path.join("/tmp", "dlna-server-" + str(os.getuid()))


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
            # Echo a byte back so the client knows the payload was fully
            # consumed before it closes. Otherwise the client's close can
            # race the server's recv(): if the client wins, recv returns
            # b"" (EOF) instead of the newline and the assertion below
            # spuriously fails.
            conn.sendall(b"ack")
            conn.close()
            srv.close()

        t = threading.Thread(target=server, daemon=True)
        t.start()

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
                # Socket file exists but the server thread may not have
                # entered listen() yet; connect() refuses until it has.
                if time.time() > deadline:
                    raise
                time.sleep(0.01)
        cli.sendall(b"\n")
        # Block until the server echoes, confirming it already read our data;
        # only then close, so there is no recv/close ordering race.
        cli.recv(3)
        cli.close()
        t.join(timeout=3)

        # Should not crash; empty string after strip.
        assert received == [b"\n"]

    def test_show_command_received(self, rundir: str):
        """Server receives "show" when client sends "show\n"."""
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
        deadline = time.time() + 5
        while not os.path.exists(sock_path):
            time.sleep(0.01)
            if time.time() > deadline:
                pytest.fail("Server socket not created in time")

        for msg in (b"show\n", b"sources:/media/video\n"):
            cli = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            while True:
                try:
                    cli.connect(sock_path)
                    break
                except ConnectionRefusedError:
                    if time.time() > deadline:
                        raise
                    time.sleep(0.01)
            cli.sendall(msg)
            cli.close()
        t.join(timeout=3)

        assert received == ["show", "sources:/media/video"]

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
    """A writable directory for lock / socket files.

    Lives under /tmp rather than the repo tmp/ (which in WSL sits on the
    drvfs-backed C: mount) because AF_UNIX bind() is unsupported on drvfs
    (errno 95 EOPNOTSUPP). flock() works on both, but the socket protocol
    tests require a real Unix filesystem.
    """
    d = tempfile.mkdtemp(prefix="dlna-si-", dir="/tmp")
    yield d
    shutil.rmtree(d, ignore_errors=True)


class TestSecondLaunchSupersedesFirst:
    """A POSIX second launch never forwards "show" (that is GUI-only). It
    always kills and replaces the running instance with its own args, exits
    quickly, and the follow-up print hook reads the NEW instance's sources.
    """

    def test_second_launch_replaces_first(self, dlna_binary, tmp_path):
        import subprocess
        config_dir = tmp_path / "config"
        media_dir = tmp_path / "media"
        config_dir.mkdir()
        media_dir.mkdir()

        env = os.environ.copy()
        env["XDG_CONFIG_HOME"] = str(config_dir)
        env["HOME"] = str(config_dir)
        env["DLNA_SERVER_SKIP_FIREWALL"] = "1"

        # First instance runs in the foreground with --debug so we can wait
        # on its exit to prove the second launch supersedes it.
        proc_a = subprocess.Popen(
            [dlna_binary, "--debug", "--source", str(media_dir)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        try:
            # A acquires the fixed global lock before StartListening() binds
            # its socket; wait for the socket so B reliably finds A.
            sock_path = Path(_global_instance_dir()) / "dlna-server.sock"
            deadline = time.time() + 10
            while not sock_path.exists():
                if proc_a.poll() is not None:
                    pytest.fail("Instance A exited before binding its socket")
                if time.time() > deadline:
                    pytest.fail("Instance A never bound its socket")
                time.sleep(0.05)

            # Launch B immediately after A's socket appears, exercising the
            # kill-and-reacquire race. --no-debug keeps B's launch
            # deterministic even if a config DebugLog=1 is persisted
            # somewhere in the inherited environment.
            start = time.time()
            result_b = subprocess.run(
                [dlna_binary, "--no-debug", "--source", str(media_dir)],
                capture_output=True,
                text=True,
                timeout=10,
                env=env,
            )
            elapsed = time.time() - start
            assert elapsed < 2.0, (
                f"Second instance took {elapsed:.2f}s to take over: "
                f"{result_b.stdout} {result_b.stderr}"
            )
            assert result_b.returncode == 0, result_b.stderr

            # A must be stopped by B's kill, proving replacement.
            proc_a.wait(timeout=10)
            assert proc_a.returncode == 0

            # The running instance is B's server, which serves the same media
            # dir; the follow-up print hook must reflect the RUNNING
            # instance's sources, not an empty standalone config.
            query = subprocess.run(
                [dlna_binary, "--print-effective-media-sources"],
                capture_output=True,
                text=True,
                timeout=10,
                env=env,
            )
            assert str(media_dir) in query.stdout
        finally:
            subprocess.run(
                [dlna_binary, "--kill-server"],
                capture_output=True, text=True, timeout=10, env=env,
            )
            proc_a.terminate()
            try:
                proc_a.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc_a.kill()
                proc_a.wait(timeout=3)


# fcntl is imported inside test functions that use it because it is
# POSIX-only and would cause ImportError during module collection on Windows.
