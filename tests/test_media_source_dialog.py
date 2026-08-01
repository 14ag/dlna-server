import subprocess
import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# Adjust these two paths to match this repository's actual build output
# locations; they are placeholders matching CMakeLists.txt's target names
# (dlna-server on Win32 and POSIX, OUTPUT_NAME "DLNA Server" on Win32).
WIN32_EXE = REPO_ROOT / "output" / "winx64" / "DLNA Server.exe"
POSIX_EXE = REPO_ROOT / "output" / "linux" / "dlna-server"


def _run(exe, *args):
    result = subprocess.run(
        [str(exe), *args],
        capture_output=True,
        text=True,
        timeout=10,
    )
    return result


def test_movie_title_from_path_handles_max_path_plus_input():
    """F-01 regression: a >=MAX_PATH-length path must not crash the
    process. SourceStemName has no fixed-size buffer, so this must exit
    0 and print a non-empty title instead of aborting."""
    if not WIN32_EXE.exists():
        return  # F-01 fix is Win32-only; skip on non-Windows build agents
    long_path = "C:\\" + ("a" * 40 + "\\") * 8 + "Movie Title.mkv"  # > 260 chars
    assert len(long_path) >= 260
    result = _run(WIN32_EXE, "--print-movie-title-from-path", long_path)
    assert result.returncode == 0
    assert result.stdout.strip() == "Movie Title"


def test_movie_title_from_path_short_path_regression():
    if not WIN32_EXE.exists():
        return
    result = _run(WIN32_EXE, "--print-movie-title-from-path", "C:\\Media\\Song.mp3")
    assert result.returncode == 0
    assert result.stdout.strip() == "Song"


def test_default_playlist_path_matches_config_dir():
    """F-02 equivalence check: refactor must not change the derived
    path's value for any real GetConfigPath() output."""
    exe = WIN32_EXE if WIN32_EXE.exists() else POSIX_EXE
    config_path = _run(exe, "--print-config-path").stdout.strip()
    default_playlist_path = _run(exe, "--print-default-playlist-path").stdout.strip()
    if exe is WIN32_EXE:
        config_dir = config_path.rsplit("\\", 1)[0]
        assert default_playlist_path == config_dir + "\\default.m3u"
    else:
        config_dir = config_path.rsplit("/", 1)[0]
        assert default_playlist_path == config_dir + "/default.m3u"


def test_media_source_file_extensions_symmetric_across_platforms():
    """FEAT-01: the shared extension list must be identical on both
    platforms -- proves media_source_file_types.h is genuinely the
    single source of truth for both native dialog implementations."""
    have_win32 = WIN32_EXE.exists()
    have_posix = POSIX_EXE.exists()
    assert have_win32 or have_posix, "no built binary available to test against"

    exts = {}
    if have_win32:
        out = _run(WIN32_EXE, "--print-media-source-file-extensions").stdout
        exts["win32"] = sorted(line.strip() for line in out.splitlines() if line.strip())
    if have_posix:
        out = _run(POSIX_EXE, "--print-media-source-file-extensions").stdout
        exts["posix"] = sorted(line.strip() for line in out.splitlines() if line.strip())

    if have_win32 and have_posix:
        assert exts["win32"] == exts["posix"]

    sample = next(iter(exts.values()))
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
    src = (REPO_ROOT / "src" / "fltk_gui_main.cpp").read_text(encoding="utf-8")
    assert "playlistButton" not in src
