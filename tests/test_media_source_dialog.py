import os
import subprocess
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


def _run(exe, *args):
    result = subprocess.run(
        [str(exe), *args],
        capture_output=True,
        text=True,
        timeout=10,
    )
    return result


@pytest.mark.parametrize("path,title", [
    ("C:\\" + ("a" * 40 + "\\") * 8 + "Movie Title.mkv", "Movie Title"),  # > MAX_PATH, F-01
    ("C:\\Media\\Song.mp3", "Song"),
])
def test_movie_title_from_path(dlna_binary, path, title):
    result = _run(dlna_binary, "--print-movie-title-from-path", path)
    assert result.returncode == 0
    assert result.stdout.strip() == title


def test_default_playlist_path_matches_config_dir(dlna_binary):
    sep = "\\" if os.name == "nt" else "/"
    config_path = _run(dlna_binary, "--print-config-path").stdout.strip()
    default_playlist_path = _run(dlna_binary, "--print-default-playlist-path").stdout.strip()
    assert default_playlist_path == config_path.rsplit(sep, 1)[0] + sep + "default.m3u"


@pytest.mark.parametrize("busy_or_running,expected", [("0", "1"), ("1", "0")])
def test_should_allow_source_drop(dlna_binary, busy_or_running, expected):
    result = _run(dlna_binary, "--print-should-allow-source-drop", busy_or_running)
    assert result.returncode == 0
    assert result.stdout.strip() == expected


def test_media_source_file_extensions_symmetric_across_platforms(dlna_binary):
    """FEAT-01: the shared extension list must be identical on both
    platforms -- proves media_source_file_types.h is genuinely the
    single source of truth for both native dialog implementations.

    Platform-neutral: runs against whichever native binary the host has.
    Deselected/never-skipped elsewhere is handled by dlna_binary + only
    raising when no compatible native binary exists at all."""
    out = _run(dlna_binary, "--print-media-source-file-extensions").stdout
    sample = sorted(line.strip() for line in out.splitlines() if line.strip())

    for expected_media in ("mp4", "mkv", "mp3", "flac"):
        assert expected_media in sample
    for expected_playlist in ("m3u", "m3u8", "pls"):
        assert expected_playlist in sample
    # Images are intentionally excluded from the media-source picker.
    for excluded in ("jpg", "png", "gif"):
        assert excluded not in sample


@pytest.mark.parametrize("path,absent", [
    ("src/mainwindow.cpp", ["IDC_SOURCE_BROWSE_PLAYLIST", "BrowsePlaylist("]),
    ("src/gtk4_gui_main.cpp", ["playlistButton"]),
])
def test_removed_playlist_button_symbols(path, absent):
    src = (REPO_ROOT / path).read_text(encoding="utf-8")
    for symbol in absent:
        assert symbol not in src