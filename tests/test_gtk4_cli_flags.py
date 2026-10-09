import os
import subprocess
import time
from pathlib import Path
import pytest

pytestmark = pytest.mark.needs_xvfb

@pytest.mark.posix_only
def test_kill_server_noop_when_not_running(dlna_server_gui_binary, xvfb):
    result = subprocess.run(
        [dlna_server_gui_binary, "--kill-server"],
        capture_output=True, timeout=10, env=xvfb,
    )
    assert result.returncode == 0

@pytest.mark.posix_only
def test_source_override_hotswaps_running_instance(tmp_path, dlna_server_gui_binary, xvfb):
    src_a = tmp_path / "a"; src_a.mkdir()
    src_b = tmp_path / "b"; src_b.mkdir()
    first = subprocess.Popen(
        [dlna_server_gui_binary, "--source", f'"{src_a}"'],
        env=xvfb,
    )
    try:
        # Wait until the first instance actually holds the singleton
        # socket. A fixed sleep races slow startup: if the second
        # process launches before the lock is held it becomes a second
        # primary instead of forwarding, and never exits.
        sock_path = (Path("/tmp") / f"com.github.dlna_server_14ag-{os.getuid()}"
                     / "com.github.dlna_server_14ag.sock")
        deadline = time.time() + 20
        while time.time() < deadline:
            if sock_path.exists():
                break
            time.sleep(0.25)
        assert sock_path.exists(), "first instance never took the singleton socket"
        second = subprocess.run(
            [dlna_server_gui_binary, "--source", f'"{src_b}"'],
            capture_output=True, timeout=10, env=xvfb,
        )
        assert second.returncode == 0
        # the second process must not have acquired its own window it should
        # have forwarded the override and exited
    finally:
        subprocess.run([dlna_server_gui_binary, "--kill-server"], timeout=10)
        first.wait(timeout=10)
