import subprocess
import pytest

@pytest.mark.posix_only
def test_print_flag_never_echoes_even_with_debug(tmp_path, dlna_server_binary):
    result = subprocess.run(
        [dlna_server_binary, "--debug", "--print-dlna-server-header"],
        capture_output=True, text=True, timeout=5,
    )
    assert result.returncode == 0
    # exactly one line the header itself nothing else on stdout or stderr
    assert result.stdout.count("\n") == 1
    assert result.stderr == ""

@pytest.mark.posix_only
def test_print_flag_never_holds_session(dlna_server_binary):
    result = subprocess.run(
        [dlna_server_binary, "--print-max-client-threads"],
        capture_output=True, text=True, timeout=3,
    )
    assert result.returncode == 0