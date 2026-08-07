import subprocess
import time
import pytest

@pytest.mark.posix_only
@pytest.mark.gui_only
def test_kill_server_noop_when_not_running(dlna_server_gui_binary, xvfb):
    result = subprocess.run(
        [dlna_server_gui_binary, "--kill-server"],
        capture_output=True, timeout=10, env=xvfb,
    )
    assert result.returncode == 0

@pytest.mark.posix_only
@pytest.mark.gui_only
def test_source_override_hotswaps_running_instance(tmp_path, dlna_server_gui_binary, xvfb):
    src_a = tmp_path / "a"; src_a.mkdir()
    src_b = tmp_path / "b"; src_b.mkdir()
    first = subprocess.Popen(
        [dlna_server_gui_binary, "--source", f'"{src_a}"'],
        env=xvfb,
    )
    try:
        time.sleep(3)
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