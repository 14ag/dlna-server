import subprocess

def test_network_change_restart_skips_when_endpoints_unchanged(dlna_binary, media_source_dir):
    (media_source_dir / "a.mp3").write_bytes(b"\x00" * 128)
    result = subprocess.run(
        [dlna_binary, "--source", str(media_source_dir),
         "--print-network-change-restart-noop-detection", "--debug"],
        capture_output=True, text=True, timeout=30,
    )
    lines = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    assert lines.get("start-ok") == "1"
    assert lines.get("saw-skip-message") == "1"
    assert lines.get("saw-fresh-multicast-join") == "0"