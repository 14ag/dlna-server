import os
import subprocess


def test_server_health_detects_http_worker_death(dlna_binary, tmp_path):
    media_dir = tmp_path / "media"
    media_dir.mkdir()
    (media_dir / "placeholder.mp3").write_bytes(b"\x00" * 32)

    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"

    result = subprocess.run(
        [dlna_binary, "--source", str(media_dir),
         "--print-server-health-detects-http-death"],
        capture_output=True, text=True, timeout=60, env=env)

    output = result.stdout
    assert "start-ok=1" in output, result.stderr
    assert "healthy-before=1" in output, output
    assert "running-after-http-death=1" in output, output
    assert "healthy-after-http-death=0" in output, output
