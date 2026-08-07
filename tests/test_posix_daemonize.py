import subprocess
import time
import pytest

@pytest.mark.posix_only
def test_debug_off_prints_ready_and_frees_terminal(tmp_path, dlna_server_binary):
    src = tmp_path / "media"; src.mkdir()
    result = subprocess.run(
        [dlna_server_binary, "--port", "18221", "--source", f'"{src}"'],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0
    assert result.stdout.strip() == "server is up"
    subprocess.run([dlna_server_binary, "--kill-server"], timeout=10)

@pytest.mark.posix_only
def test_debug_on_holds_foreground(tmp_path, dlna_server_binary):
    src = tmp_path / "media"; src.mkdir()
    proc = subprocess.Popen(
        [dlna_server_binary, "--debug", "--port", "18281", "--source", f'"{src}"'],
        stdout=subprocess.PIPE,
    )
    try:
        with pytest.raises(subprocess.TimeoutExpired):
            proc.communicate(timeout=2)
    finally:
        subprocess.run([dlna_server_binary, "--kill-server"], timeout=10)
        proc.wait(timeout=10)