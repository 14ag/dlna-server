from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_m3u8_is_registered_as_streamable_playlist_media():
    source = read_source("src/dlna_utils.cpp")

    assert 'L".m3u8"' in source
    assert 'L"application/vnd.apple.mpegurl"' in source
    assert 'L"object.item.videoItem"' in source


def test_playlist_scanners_recurse_into_nested_m3u8_with_depth_guard():
    header = read_source("src/media_sources.h")
    assert "ScanPlaylist(MediaIndexState& state, const ConfigSnapshot& cfg, const std::wstring& playlistPath, int parentId, int depth" in header

    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        source = read_source(path)
        scan_start = source.index("void MediaSources::ScanPlaylist")
        scan_end = source.index("void MediaSources::ScanNetworkFolder", scan_start)
        scan_body = source[scan_start:scan_end]

        assert "depth > 8" in scan_body
        assert "IsPlaylistSourcePath(entry.location)" in scan_body
        assert "ScanPlaylist(state, cfg, entry.location, playlistFolder.id, depth + 1)" in scan_body
        assert "AddMediaFile(state, cfg, entry.location, parentId, entry.title, entry.subtitlePath)" in scan_body


def test_rejected_media_extension_logs_redacted_path():
    for path, arg_name in (("src/media_sources.cpp", "path"), ("src/posix_media_sources.cpp", "pathText")):
        source = read_source(path)
        add_start = source.index("void MediaSources::AddMediaFile")
        add_end = source.index("if (!state.duplicateKeys", add_start)
        add_body = source[add_start:add_end]

        assert "[media:reject-extension]" in add_body
        assert "RedactUrlForLog" in add_body
        assert f"RedactUrlForLog({arg_name})" in add_body


def test_curl_remote_fetch_uses_user_agent_ftp_options_low_speed_and_redacted_logs():
    source = read_source("src/network_sources.cpp")

    for token in (
        "CURLOPT_USERAGENT",
        "DLNA-Server/",
        "CURLOPT_FTP_RESPONSE_TIMEOUT",
        "CURLOPT_FTP_USE_EPSV",
        "CURLOPT_LOW_SPEED_LIMIT",
        "CURLOPT_LOW_SPEED_TIME",
        "RedactUrlForLog",
    ):
        assert token in source

    assert "authentication failed for %ls\", RedactUrlForLog(url).c_str()" in source
    assert "Remote content truncated after %d bytes: %ls\", kMaxCurlOutputBytes, RedactUrlForLog(url).c_str()" in source
    assert "user:pass@" not in source
