import os
import subprocess


def test_media_item_resolvable_during_rescan(dlna_binary, tmp_path):
    media_dir = tmp_path / "media"
    media_dir.mkdir()
    for i in range(500):
        (media_dir / f"track{i:04d}.mp3").write_bytes(b"\x00" * 32)

    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"

    result = subprocess.run(
        [dlna_binary, "--source", str(media_dir),
         "--print-media-item-stays-resolvable-during-rescan"],
        capture_output=True, text=True, timeout=120, env=env)

    output = result.stdout
    assert "start-ok=1" in output, result.stderr
    assert "ever-missing=0" in output, output
