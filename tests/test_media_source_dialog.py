import subprocess
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# Adjust these two paths to match this repository's actual build output
# locations; they are placeholders matching CMakeLists.txt's target names
# (dlna-server on Win32 and POSIX, OUTPUT_NAME "DLNA Server" on Win32).
WIN32_EXE = REPO_ROOT / "output" / "winx64" / "DLNA Server.exe"
POSIX_EXE = REPO_ROOT / "output" / "linux" / "dlna-server"


def _native_exe():
    """Return the binary whose executable format matches the local host OS.

    A cross-built tree (e.g. output/linux/ present on a Windows checkout, or
    output/winx64/ present on a WSL checkout) can leave both EXE paths
    existing at once. subprocess.run on the wrong-format binary raises
    OSError (WinError 193 on Windows, Exec format error on POSIX), which
    previously made test_media_source_file_extensions_symmetric_across_platforms
    flaky-to-failing depending on which platform last built the tree. Pick the
    one that matches sys.platform instead of whichever happens to exist."""
    if sys.platform == "win32":
        return WIN32_EXE
    return POSIX_EXE


def _run(exe, *args):
    result = subprocess.run(
        [str(exe), *args],
        capture_output=True,
        text=True,
        timeout=10,
    )
    return result


@pytest.mark.windows_only
def test_movie_title_from_path_handles_max_path_plus_input():
    """F-01 regression: a >=MAX_PATH-length path must not crash the
    process. SourceStemName has no fixed-size buffer, so this must exit
    0 and print a non-empty title instead of aborting.

    Win32-only flag (--print-movie-title-from-path is not implemented in
    posix_main.cpp); deselected on POSIX so it never reports as skipped."""
    long_path = "C:\\" + ("a" * 40 + "\\") * 8 + "Movie Title.mkv"  # > 260 chars
    assert len(long_path) >= 260
    result = _run(WIN32_EXE, "--print-movie-title-from-path", long_path)
    assert result.returncode == 0
    assert result.stdout.strip() == "Movie Title"


@pytest.mark.windows_only
def test_movie_title_from_path_short_path_regression():
    result = _run(WIN32_EXE, "--print-movie-title-from-path", "C:\\Media\\Song.mp3")
    assert result.returncode == 0
    assert result.stdout.strip() == "Song"


@pytest.mark.posix_only
def test_movie_title_from_path_handles_max_path_plus_input_posix():
    long_path = "C:\\" + ("a" * 40 + "\\") * 8 + "Movie Title.mkv"  # > 260 chars
    assert len(long_path) >= 260
    result = _run(POSIX_EXE, "--print-movie-title-from-path", long_path)
    assert result.returncode == 0
    assert result.stdout.strip() == "Movie Title"


@pytest.mark.posix_only
def test_movie_title_from_path_short_path_regression_posix():
    result = _run(POSIX_EXE, "--print-movie-title-from-path", "C:\\Media\\Song.mp3")
    assert result.returncode == 0
    assert result.stdout.strip() == "Song"


@pytest.mark.windows_only
def test_default_playlist_path_matches_config_dir():
    """F-02 equivalence check: refactor must not change the derived
    path's value for any real GetConfigPath() output.

    Win32-only flag (--print-default-playlist-path is not implemented in
    posix_main.cpp; on POSIX the flag would be parsed as a media-source
    path and the server would start). Deselected on POSIX so it never
    runs, never skips."""
    exe = WIN32_EXE
    config_path = _run(exe, "--print-config-path").stdout.strip()
    default_playlist_path = _run(exe, "--print-default-playlist-path").stdout.strip()
    config_dir = config_path.rsplit("\\", 1)[0]
    assert default_playlist_path == config_dir + "\\default.m3u"


@pytest.mark.posix_only
def test_default_playlist_path_matches_config_dir_posix():
    """F-02 POSIX mirror: the printed path must end in default.m3u and
    sit in the same directory as GetConfigPath(). POSIX config paths use
    forward slashes, so the expected path is derived inside the test
    from the actual --print-config-path output."""
    exe = POSIX_EXE
    config_path = _run(exe, "--print-config-path").stdout.strip()
    default_playlist_path = _run(exe, "--print-default-playlist-path").stdout.strip()
    config_dir = config_path.rsplit("/", 1)[0]
    assert default_playlist_path == config_dir + "/default.m3u"


@pytest.mark.posix_only
def test_should_allow_source_drop_true_input_posix():
    result = _run(POSIX_EXE, "--print-should-allow-source-drop", "0")
    assert result.returncode == 0
    assert result.stdout.strip() == "1"


@pytest.mark.posix_only
def test_should_allow_source_drop_false_input_posix():
    result = _run(POSIX_EXE, "--print-should-allow-source-drop", "1")
    assert result.returncode == 0
    assert result.stdout.strip() == "0"


def test_media_source_file_extensions_symmetric_across_platforms():
    """FEAT-01: the shared extension list must be identical on both
    platforms -- proves media_source_file_types.h is genuinely the
    single source of truth for both native dialog implementations.

    Platform-neutral: runs against whichever native binary the host has.
    Deselected/never-skipped elsewhere is handled by _native_exe + only
    raising when no compatible native binary exists at all."""
    native = _native_exe()
    if not native.exists():
        raise RuntimeError(
            f"no native binary built at {native}; build it and re-run this test"
        )

    out = _run(native, "--print-media-source-file-extensions").stdout
    sample = sorted(line.strip() for line in out.splitlines() if line.strip())

    for expected_media in ("mp4", "mkv", "mp3", "flac"):
        assert expected_media in sample
    for expected_playlist in ("m3u", "m3u8", "pls"):
        assert expected_playlist in sample
    # Images are intentionally excluded from the media-source picker.
    for excluded in ("jpg", "png", "gif"):
        assert excluded not in sample


def test_playlist_button_identifier_removed_win32():
    """Source-invariant guard, not a binary-behavior test: confirms the
    removed control ID and callback do not silently reappear (e.g. via
    a bad merge). Run against source, not the compiled binary."""
    src = (REPO_ROOT / "src" / "mainwindow.cpp").read_text(encoding="utf-8")
    assert "IDC_SOURCE_BROWSE_PLAYLIST" not in src
    assert "BrowsePlaylist(" not in src  # BrowseMediaFile( remains, this must not


def test_playlist_button_identifier_removed_posix():
    src = (REPO_ROOT / "src" / "gtk4_gui_main.cpp").read_text(encoding="utf-8")
    assert "playlistButton" not in src
