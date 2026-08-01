import subprocess

import pytest


def _run(binary_path, *args, timeout=30):
    return subprocess.run(
        [str(binary_path), *args],
        capture_output=True, text=True, timeout=timeout,
    )


def test_thread_guard_survives_exception(dlna_binary):
    """Task 1.1/1.4: an exception inside RunGuarded must not crash the process."""
    result = _run(dlna_binary, "--print-thread-guard-behavior")
    assert result.returncode == 0
    assert "guard-caught-exception=1" in result.stdout


def test_single_file_media_source_is_scanned(dlna_binary, tmp_path):
    """F-FEAT-01 / Task 2.1: a single file source must produce exactly
    one leaf media item, where before the fix it produced zero."""
    media_file = tmp_path / "sample.mp4"
    media_file.write_bytes(b"\x00" * 4096)  # scanner only reads size, not codec content

    result = _run(dlna_binary, "--source", str(media_file),
                  "--print-single-file-source-scan")
    assert result.returncode == 0
    assert "leaf-media-items=1" in result.stdout
    assert "first-mime=video/mp4" in result.stdout
    assert "first-class=object.item.videoItem" in result.stdout


def test_single_audio_file_media_source_is_scanned(dlna_binary, tmp_path):
    """F-FEAT-01 / Task 2.1: audio single-file source routes through the
    same single-file branch and must emit the correct audio UPNP class."""
    media_file = tmp_path / "sample.mp3"
    media_file.write_bytes(b"\x00" * 4096)

    result = _run(dlna_binary, "--source", str(media_file),
                  "--print-single-file-source-scan")
    assert result.returncode == 0
    assert "leaf-media-items=1" in result.stdout
    assert "first-class=object.item.audioItem.musicTrack" in result.stdout
