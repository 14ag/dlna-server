import subprocess
import time
import pytest

@pytest.mark.posix_only
def test_second_launch_replaces_first_with_new_source(tmp_path, dlna_server_binary):
    src_a = tmp_path / "a"; src_a.mkdir()
    src_b = tmp_path / "b"; src_b.mkdir()
    (src_b / "clip.mp4").write_bytes(b"\x00" * 16)

    first = subprocess.Popen(
        [dlna_server_binary, "--no-debug", "--port", "18211", "--source", f'"{src_a}"'],
        stdout=subprocess.PIPE,
    )
    try:
        time.sleep(2)
        # real second launch replaces the first instance with its own args
        second = subprocess.run(
            [dlna_server_binary, "--no-debug", "--port", "18211", "--source", f'"{src_b}"'],
            capture_output=True, text=True, timeout=15,
        )
        # wait for the second instance to finish starting then read the
        # effective sources back through a follow-up print hook
        time.sleep(2)
        third = subprocess.run(
            [dlna_server_binary, "--print-effective-media-sources"],
            capture_output=True, text=True, timeout=15,
        )
        # the running instance's sources must now be the second one
        assert str(src_b) in third.stdout
    finally:
        subprocess.run([dlna_server_binary, "--kill-server"], timeout=10)
        first.wait(timeout=10)