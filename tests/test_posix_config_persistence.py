import subprocess
import pytest

@pytest.mark.posix_only
def test_port_flag_persists_to_config(tmp_path, dlna_server_binary, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    src = tmp_path / "media"; src.mkdir()
    proc = subprocess.Popen(
        [dlna_server_binary, "--port", "18299", "--source", f'"{src}"'],
        stdout=subprocess.PIPE,
    )
    proc.communicate(timeout=10)
    result = subprocess.run(
        [dlna_server_binary, "--print-config-path"],
        capture_output=True, text=True, timeout=5,
    )
    config_path = result.stdout.strip()
    config_text = open(config_path, encoding="utf-8-sig").read()
    assert "Port=18299" in config_text
    subprocess.run([dlna_server_binary, "--kill-server"], timeout=5)