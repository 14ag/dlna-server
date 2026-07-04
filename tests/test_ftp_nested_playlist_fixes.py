from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_m3u8_is_never_added_as_a_playable_media_item():
    source = read_source("src/dlna_utils.cpp")
    assert 'L".m3u8"' not in source

    for path, add_media_call in (
        ("src/media_sources.cpp", "void MediaSources::AddMediaFile"),
        ("src/posix_media_sources.cpp", "void MediaSources::AddMediaFile"),
    ):
        scan_source = read_source(path)
        for call_site in ("void MediaSources::ScanFolder", "void MediaSources::ScanPlaylist", "void MediaSources::ScanNetworkFolder"):
            start = scan_source.index(call_site)
            end = scan_source.index("\n}\n", start)
            body = scan_source[start:end]
            if "AddMediaFile(state" in body:
                assert "IsPlaylistSourcePath" in body


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


def test_hls_manifest_is_exposed_as_a_single_item_not_exploded():
    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        source = read_source(path)
        scan_start = source.index("void MediaSources::ScanPlaylist")
        scan_end = source.index("void MediaSources::ScanNetworkFolder", scan_start)
        scan_body = source[scan_start:scan_end]

        assert "IsHlsPlaylistSource(playlistPath)" in scan_body
        assert "AddHlsStreamItem(state, playlistPath, parentId)" in scan_body
        # the HLS branch must return before the generic entry-walking loop runs
        assert scan_body.index("IsHlsPlaylistSource(playlistPath)") < scan_body.index("LoadPlaylistEntries(")

        add_hls_start = source.index("MediaSources::AddHlsStreamItem")
        add_hls_body = source[add_hls_start:add_hls_start + 1600]
        assert 'L"application/vnd.apple.mpegurl"' in add_hls_body
        assert "IsAllowedExtension(" not in add_hls_body


def test_hls_item_defaults_to_direct_exposure_when_not_proxied():
    content = read_source("src/contentdirectory.cpp")
    # exposeRemoteDirect must remain gated on proxyStreams / ShouldProxyRemoteUrl
    assert "exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !cfg.proxyStreams && !ShouldProxyRemoteUrl(it.path)" in content


def test_hls_referenced_by_another_playlist_is_not_double_wrapped():
    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        source = read_source(path)
        scan_start = source.index("void MediaSources::ScanPlaylist")
        scan_end = source.index("void MediaSources::ScanNetworkFolder", scan_start)
        scan_body = source[scan_start:scan_end]

        assert "IsHlsPlaylistSource(entry.location)" in scan_body
        assert "AddHlsStreamItem(state, entry.location, parentId, entry.title)" in scan_body
        assert scan_body.index("if (IsPlaylistSourcePath(entry.location)) {") < scan_body.index("IsHlsPlaylistSource(entry.location)")
        assert scan_body.index("IsHlsPlaylistSource(entry.location)") < scan_body.index("MediaItem playlistFolder")


def test_playlist_fetch_failure_is_distinguished_from_empty_playlist():
    header = read_source("src/network_sources.h")
    network_source = read_source("src/network_sources.cpp")

    assert "bool* fetchFailed = nullptr" in header
    assert "std::vector<PlaylistEntry> LoadPlaylistEntries(const std::wstring& playlistPath, bool* fetchFailed) {" in network_source
    assert "std::string ReadSourceText(const std::wstring& source, bool* ok = nullptr) {" in network_source

    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        scan_source = read_source(path)
        assert "LoadPlaylistEntries(playlistPath, &fetchFailed)" in scan_source
        assert "[media:fetch-failed]" in scan_source
        assert (
            'RecordScanError(BuildStableContainerKey(parentId, SourceStemName(playlistPath), playlistPath, g_canonicalize), L"Playlist fetch failed")'
            in scan_source
        )
