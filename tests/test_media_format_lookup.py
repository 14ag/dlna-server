import pytest


KNOWN_EXTENSIONS_AND_MIME = {
    ".mp4": "video/mp4",
    ".mkv": "video/x-matroska",
    ".mp3": "audio/mpeg",
    ".flac": "audio/flac",
    ".jpg": "image/jpeg",
}


@pytest.mark.parametrize("ext,expected_mime", KNOWN_EXTENSIONS_AND_MIME.items())
def test_media_format_lookup_matches_known_extensions(dlna_binary, ext, expected_mime):
    result = dlna_binary_process(dlna_binary, ["--print-media-format-lookup", ext])
    lines = result.stdout.strip().splitlines()
    assert lines[0] == expected_mime


def test_media_format_lookup_reports_no_match_for_unknown_extension(dlna_binary):
    result = dlna_binary_process(dlna_binary, ["--print-media-format-lookup", ".doesnotexist"])
    assert result.stdout.strip() == "no-match"


def dlna_binary_process(binary, args):
    import subprocess
    return subprocess.run(
        [binary] + args, capture_output=True, text=True, timeout=15,
    )