import subprocess
import pytest

@pytest.mark.posix_only
def test_no_sources_message_and_exit_code(tmp_path, dlna_server_binary, monkeypatch):
    # empty XDG_CONFIG_HOME so no prior config.ini with sources leaks in
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    result = subprocess.run([dlna_server_binary], capture_output=True, text=True, timeout=10)
    assert result.returncode == 1
    assert "no sources found, please add a source or pass one with the --source flag" in result.stderr