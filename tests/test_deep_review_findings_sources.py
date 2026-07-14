from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_server_scan_lifecycle_does_not_join_under_scan_mutex():
    server = read("src/server.cpp")
    posix = read("src/posix_server.cpp")
    header = read("src/server.h")

    for source in (server, posix):
        assert "std::thread previousScan" in source
        assert "ShouldStartScan()" in source
        assert "m_stopping.store(true" in source
        assert "m_endpointMutex" in source
    assert "std::atomic<bool> m_running" in header
    assert "mutable std::mutex m_endpointMutex" in header


def test_http_routes_validate_query_host_post_and_send_all_binary():
    http = read("src/httpserver.cpp")
    posix = read("src/posix_httpserver.cpp")

    for source in (http, posix):
        assert "SplitRequestTarget" in source
        assert "ValidateHostHeader" in source
        assert "Content-Length header required" in source
        assert "Accept-Ranges: none" in source
    assert "if (!GetFileSizeEx" in http
    assert "TrySendAll(clientSocket, buf" in posix


def test_xml_extraction_handles_attributes_and_escapes_uuid():
    content = read("src/contentdirectory.cpp")

    assert "FindStartTagWithAttributes" in content
    assert "ExtractTagValue" in content
    assert "std::string deviceUUID = XMLEscapeUtf8" in content
    assert "ApplyDidlFilter" in content
    assert "upnp:class =" in content


def test_remote_parsers_reject_truncation_sparse_pls_and_bad_roots():
    network = read("src/network_sources.cpp")

    assert "truncated" in network
    assert "kMaxPlsIndex" in network
    assert "ParseUnixListName" in network
    assert "ClassifyRemoteDirectoryEntry" in network
    assert "ParseUrlForJoin" in network
    assert "[remote:auth]" in network
    assert "HTTP directory listing is not supported" in network


def test_gena_queue_bounded_initial_notify_and_response_status():
    event_h = read("src/upnp_eventing.h")
    event = read("src/upnp_eventing.cpp")

    assert "m_generation" in event_h
    assert "kMaxQueuedNotifyJobs" in event
    assert "QueueInitialNotifyLocked" in event
    assert "CURLINFO_RESPONSE_CODE" in event
    assert "ExpireSubscription" in event
    assert "coalesced" in event


def test_ssdp_queue_bounded_send_errors_and_empty_drop():
    ssdp = read("src/ssdp.cpp") + read("src/posix_ssdp.cpp") + read("src/ssdp.h")

    assert "kMaxDelayedResponses" in ssdp
    assert "CoalesceDelayedResponse" in ssdp
    assert "responses.empty()" in ssdp
    assert "SSDP send failed" in ssdp
    assert "IP_MULTICAST_IF failed" in ssdp


def test_narrow_ascii_uses_utf8_conversion():
    utils = read("src/dlna_utils.cpp")

    assert "#include \"netutils.h\"" in utils
    assert "std::string NarrowAscii(const std::wstring& value)" in utils
    assert "return WideToUtf8(value);" in utils
    assert "static_cast<char>(ch)" not in utils


def test_remote_source_rescan_behavior_documented():
    readme = read("README.md")
    source_watcher = read("src/source_watcher.cpp")

    assert "Remote sources" in readme
    assert "ftp://" in readme
    assert "http://" in readme
    assert "https://" in readme
    assert "smb://" not in readme
    assert "only scanned at server start" in readme
    assert "do not participate in the automatic file watch loop" in readme
    assert "Adding or removing a remote source at runtime requires a server restart" in readme
    assert "IsRemoteMediaUrl(source.path)" in source_watcher


def test_join_url_removed_resolve_playlist_entry_uses_resolve_relative_url():
    # JoinUrl was consolidated onto ResolveRelativeUrl per the TODO in
    # src/network_sources.cpp, see workflow dlna-server-scan-hang-and-
    # review-fixes-workflow-13-7-26.md task T6. JoinUrl did not collapse
    # ".." segments, ResolveRelativeUrl does.
    network = read("src/network_sources.cpp")

    assert "std::wstring JoinUrl(" not in network
    assert "std::string ParentUrl(" not in network
    idx = network.find("std::wstring ResolvePlaylistEntry(")
    assert idx > 0
    region = network[idx:idx + 400]
    assert "ResolveRelativeUrl(playlistPath, entry)" in region


def test_http_worker_limits_aligned():
    win_http = read("src/httpserver.cpp")
    posix_http = read("src/posix_httpserver.cpp")

    assert "SetThreadpoolThreadMaximum(m_threadPool, 64)" in win_http
    assert "constexpr size_t kMaxClientThreads = 64" in posix_http


def test_split_header_and_stream_timeouts():
    win_http = read("src/httpserver.cpp")
    posix_http = read("src/posix_httpserver.cpp")

    assert "SetSocketStreamTimeouts" in win_http
    assert "SetSocketStreamTimeouts" in posix_http
    assert "kStreamTimeoutMs = 60000" in win_http
    assert "timeval timeout{60, 0}" in posix_http
    assert "SO_SNDTIMEO" in win_http
    assert "SO_SNDTIMEO" in posix_http


def test_album_art_case_variants_reduced_on_windows():
    utils = read("src/dlna_utils.cpp")

    assert "BuildAlbumArtCandidateNames" in utils
    assert "#if defined(_WIN32)" in utils
    assert "Folder.jpg" not in utils.split("#if defined(_WIN32)")[1].split("#else")[0]


def test_ssdp_ttl_complies_with_upnp_spec():
    win_ssdp = read("src/ssdp.cpp")
    posix_ssdp = read("src/posix_ssdp.cpp")

    assert "kMulticastTTL = 4" in win_ssdp
    assert "IP_MULTICAST_TTL" in win_ssdp
    assert "kMulticastHops = 4" in win_ssdp
    assert "IPV6_MULTICAST_HOPS" in win_ssdp
    assert "unsigned char ttl = 4" in posix_ssdp
    assert "int hops = 4" in posix_ssdp
    assert "ComputeSsdpStartupJitterMilliseconds" in win_ssdp
    assert "ComputeSsdpNextAliveIntervalMilliseconds" in win_ssdp
    assert "ComputeSsdpStartupJitterMilliseconds" in posix_ssdp
    assert "ComputeSsdpNextAliveIntervalMilliseconds" in posix_ssdp
    assert "SSDP_ALIVE_INTERVAL_MS" not in win_ssdp
    assert "kAliveInterval = std::chrono::minutes(15)" not in posix_ssdp


def test_ssdp_jitter_helpers_are_shared_not_duplicated():
    utils_header = read("src/dlna_utils.h")
    utils_source = read("src/dlna_utils.cpp")

    assert "unsigned int ComputeSsdpStartupJitterMilliseconds();" in utils_header
    assert "unsigned int ComputeSsdpNextAliveIntervalMilliseconds();" in utils_header
    assert "unsigned int ComputeSsdpStartupJitterMilliseconds() {" in utils_source
    assert "unsigned int ComputeSsdpNextAliveIntervalMilliseconds() {" in utils_source
    assert utils_source.count("std::uniform_int_distribution<unsigned int> distribution(0, 100)") == 1


def test_release_scripts_enforce_platform_output_contracts():
    ps1 = read("scripts/build-release-assets.ps1")
    linux = read("scripts/build-linux-desktop-assets.sh")
    bat = read("build-assets.bat")
    cmake = read("CMakeLists.txt")

    assert "Test-SelectedPlatformPrerequisites" in ps1
    assert "DLNA_NO_CLEAN" in ps1 and "DLNA_MACOS_PLATFORM_DIR" in ps1
    assert "New-SourceReleaseArchive" in ps1
    assert "release_stage_dir" in linux
    assert "find \"$output_dir\" -maxdepth 1 -type f -name '*.AppImage' -delete" in linux
    assert "usr/bin/curl" not in linux
    assert "where wsl.exe" in bat
    assert "find_package(CURL REQUIRED)" in cmake
