import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class TestPlaylistContentValidation:
    @pytest.mark.parametrize("path,text,expected", [
        ("test.m3u8", "#EXTM3U\n#EXTINF:-1,Test\nhttp://example.com/stream", "1"),
        ("test.m3u", "#EXTM3U\n#EXTINF:-1,Test\nhttp://example.com/stream", "1"),
        ("test.m3u8", "<html><body>404 Not Found</body></html>", "0"),
        ("test.m3u8", "Some random text that is not a playlist", "0"),
        ("test.m3u8", "", "0"),
        ("test.pls", "[playlist]\nFile1=http://example.com/stream\nNumberOfEntries=1", "1"),
        ("test.pls", "NotAPlaylist\nSomeRandomText", "0"),
    ])
    def test_is_recognized_playlist_text(self, dlna_binary, tmp_path, path, text, expected):
        f = tmp_path / "input.txt"
        f.write_text(text, encoding="utf-8")
        result = subprocess.run(
            [dlna_binary, "--print-is-recognized-playlist", path, str(f)],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (
            f"Binary exited with code {result.returncode}: "
            f"{result.stderr.strip()}")
        assert result.stdout.strip() == expected, (
            f"path={path}, text={text!r}: expected {expected}, got {result.stdout.strip()}")

    def test_utf8_bom_is_stripped(self, dlna_binary, tmp_path):
        f = tmp_path / "input.txt"
        bom_text = "\ufeff#EXTM3U\n#EXTINF:-1,Test\nhttp://example.com/stream"
        f.write_text(bom_text, encoding="utf-8")
        result = subprocess.run(
            [dlna_binary, "--print-is-recognized-playlist", "test.m3u8", str(f)],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (
            f"Binary exited with code {result.returncode}: "
            f"{result.stderr.strip()}")
        assert result.stdout.strip() == "1", (
            f"BOM test: expected 1, got {result.stdout.strip()}")

def test_is_recognized_playlist_text_declared_in_header():
    header = read("src/network_sources.h")
    assert "bool IsRecognizedPlaylistText(const std::wstring& path, const std::string& text);" in header

def test_is_recognized_playlist_text_defined_in_source():
    source = read("src/network_sources.cpp")
    assert "bool IsRecognizedPlaylistText(const std::wstring& path, const std::string& text) {" in source
    assert 'return body.rfind("#EXTM3U", 0) == 0;' in source
    assert 'ToLowerAscii(body).rfind("[playlist]", 0) == 0' in source
    assert "body.erase(0, 3)" in source

def test_is_recognized_playlist_text_called_in_scan_one_playlist_node():
    source = read("src/media_sources_common.cpp")
    assert "IsRecognizedPlaylistText(node.path, fetched.text)" in source
    assert "Playlist content not recognized" in source
    assert "[media:fetch-invalid]" in source

def test_print_flag_added_to_main():
    win = read("src/main.cpp")
    posix = read("src/posix_main.cpp")
    assert 'wcscmp(argv[i], L"--print-is-recognized-playlist")' in win
    assert 'arg == "--print-is-recognized-playlist"' in posix
