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
    assert "ScanPlaylistTree(std::shared_ptr<PlaylistScanContext> ctx, const std::wstring& path," in header
    assert "ScanOnePlaylistNode(std::shared_ptr<PlaylistScanContext> ctx, PendingPlaylistNode node)" in header
    assert "RunPlaylistDispatcher(std::shared_ptr<PlaylistScanContext> ctx)" in header
    assert "kMaxPlaylistRecursionDepth = 8" in header

    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        source = read_source(path)
        assert "void MediaSources::ScanPlaylistTree(std::shared_ptr<PlaylistScanContext> ctx, const std::wstring& path," in source
        assert "void MediaSources::RunPlaylistDispatcher(std::shared_ptr<PlaylistScanContext> ctx)" in source
        assert "void MediaSources::ScanOnePlaylistNode(std::shared_ptr<PlaylistScanContext> ctx, PendingPlaylistNode node)" in source
        assert "node.depth > kMaxPlaylistRecursionDepth" in source
        assert "IsPlaylistSourcePath(entry.location)" in source
        assert "AddMediaFile(ctx->state, ctx->cfg, entry.location, folderId, entry.title, entry.subtitlePath)" in source


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
        scan_start = source.index("void MediaSources::ScanOnePlaylistNode")
        scan_end = source.index("void MediaSources::ScanNetworkFolder", scan_start)
        scan_body = source[scan_start:scan_end]

        assert "fetched.isHls" in scan_body
        assert "AddHlsStreamItem(ctx->state, node.path, node.parentId, node.titleOverride)" in scan_body
        # the HLS branch must return before the generic entry-walking loop runs
        assert scan_body.index("fetched.isHls") < scan_body.index("ParseFetchedPlaylistText(node.path")

        add_hls_start = source.index("MediaSources::AddHlsStreamItem")
        add_hls_body = source[add_hls_start:add_hls_start + 1600]
        assert 'L"video/mpegurl"' in add_hls_body
        assert "IsAllowedExtension(" not in add_hls_body


def test_hls_item_defaults_to_direct_exposure_when_not_proxied():
    content = read_source("src/contentdirectory.cpp")
    # exposeRemoteDirect must remain gated on proxyStreams / ShouldProxyRemoteUrl
    assert "exposeRemoteDirect = IsRemoteMediaUrl(it.path) && !proxyStreams && !ShouldProxyRemoteUrl(it.path)" in content


def test_hls_referenced_by_another_playlist_is_not_double_wrapped():
    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        source = read_source(path)
        scan_start = source.index("void MediaSources::ScanOnePlaylistNode")
        scan_end = source.index("void MediaSources::ScanNetworkFolder", scan_start)
        scan_body = source[scan_start:scan_end]

        # HLS check now happens in ScanOnePlaylistNode via fetched.isHls
        assert "if (IsPlaylistSourcePath(entry.location)) {" in scan_body
        # ScanOnePlaylistNode handles both HLS detection and empty-check
        assert "newlyDiscovered.push_back(PendingPlaylistNode{entry.location, folderId, entry.title, node.depth + 1})" in scan_body


def test_playlist_fetch_failure_is_distinguished_from_empty_playlist():
    header = read_source("src/network_sources.h")
    network_source = read_source("src/network_sources.cpp")

    assert "FetchedPlaylist FetchPlaylistOnce(const std::wstring& playlistPath)" in header
    assert "std::vector<PlaylistEntry> ParseFetchedPlaylistText" in header
    assert "std::string ReadSourceText(const std::wstring& source, bool* ok = nullptr) {" in network_source

    for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
        scan_source = read_source(path)
        assert "FetchPlaylistOnce(node.path)" in scan_source
        assert "[media:fetch-failed]" in scan_source
        assert "RecordScanError" in scan_source
        assert "BuildStableContainerKey(node.parentId, SourceStemName(node.path), node.path, g_canonicalize)" in scan_source
        assert 'L"Playlist fetch failed"' in scan_source
