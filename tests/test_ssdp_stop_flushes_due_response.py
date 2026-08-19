import subprocess

def test_ssdp_stop_flushes_due_response(dlna_binary, media_source_dir):
    (media_source_dir / "a.mp3").write_bytes(b"\x00" * 128)
    result = subprocess.run(
        [dlna_binary, "--source", str(media_source_dir),
         "--print-ssdp-stop-flushes-due-response", "--debug"],
        capture_output=True, text=True, timeout=30,
    )
    lines = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    assert lines.get("start-ok") == "1"
    assert lines.get("ran") == "1"
    assert lines.get("saw-response-sent") == "1"