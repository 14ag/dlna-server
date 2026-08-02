import subprocess
import time
import sys
import pytest
import requests


@pytest.mark.skipif(sys.platform != "win32", reason="WM_KILL_SERVER is Win32-only")
def test_kill_server_during_start_does_not_hang_or_leave_orphan(dlna_binary, tmp_path):
    source_dir = tmp_path / "media"
    source_dir.mkdir()
    (source_dir / "a.mp3").write_bytes(b"\x00" * 1024)

    proc = subprocess.Popen(
        [dlna_binary, "--source", str(source_dir), "--port", "18201"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    # Race the kill against startup deliberately: send it immediately,
    # before waiting for the server to report running. This is the
    # exact scenario F-CMR-03 describes (WM_KILL_SERVER arriving while
    # BeginStartServer's worker is still mid-flight).
    time.sleep(0.05)
    kill = subprocess.run([dlna_binary, "--kill-server"], timeout=10)
    assert kill.returncode == 0

    proc.wait(timeout=15)  # must exit -- not hang -- within a bounded time
    assert proc.returncode is not None

    # No orphaned listener left behind: a fresh instance on the same
    # port must be able to bind and serve immediately afterward.
    proc2 = subprocess.Popen(
        [dlna_binary, "--source", str(source_dir), "--port", "18201"],
    )
    try:
        deadline = time.time() + 15
        ok = False
        while time.time() < deadline:
            try:
                r = requests.get("http://127.0.0.1:18201/description.xml", timeout=1)
                if r.status_code == 200:
                    ok = True
                    break
            except requests.RequestException:
                time.sleep(0.2)
        assert ok, "second instance failed to bind/serve on the port the first instance should have released"
    finally:
        subprocess.run([dlna_binary, "--kill-server"], timeout=10)
        proc2.wait(timeout=15)
