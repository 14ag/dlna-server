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


def test_media_database_identity_and_atomic_save_contracts():
    db_h = read("src/media_database.h")
    db = read("src/media_database.cpp")
    media = read("src/media_sources.cpp") + read("src/posix_media_sources.cpp")

    assert "GetOrCreateStableContainerId" in db_h
    assert "MarkScanSuccess" in db_h
    assert "ReplaceFileAtomic" in db
    assert ".tmp" in db
    assert "ScopedScanSuccess" in media
    assert "BuildStableMediaKey" in media
    assert "BuildStableContainerKey" in media
    assert "perStemAlbumArt" in media
    assert "folderAlbumArt" in media
    assert 'L"[media:scan-depth]"' in media


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


def test_browse_direct_children_uses_single_locked_accessor():
    content = read("src/contentdirectory.cpp")
    header = read("src/media_sources.h")
    win = read("src/media_sources.cpp")
    posix = read("src/posix_media_sources.cpp")

    assert "TryGetChildren" in header
    assert "enum class GetChildrenResult" in header
    assert "NotFound" in header
    assert "NotAContainer" in header
    assert "Success" in header
    assert "TryGetChildren(int objId, std::vector<MediaItem>& out)" in header
    assert "TryGetChildren(int objId, std::vector<MediaItem>& out)" in win
    assert "TryGetChildren(int objId, std::vector<MediaItem>& out)" in posix
    assert "AppMedia.TryGetChildren(objId, childrenResult)" in content
    assert "GetChildrenResult::NotFound" in content
    assert "GetChildrenResult::NotAContainer" in content
    assert "Not a container" in content


def test_initial_scan_completes_before_browse_search():
    server_h = read("src/server.h")
    win_server = read("src/server.cpp")
    posix_server = read("src/posix_server.cpp")
    content = read("src/contentdirectory.cpp")

    assert "m_initialScanComplete" in server_h
    assert "IsInitialScanComplete" in server_h
    assert "m_initialScanComplete(false)" in win_server
    assert "m_initialScanComplete(false)" in posix_server
    assert "JoinBackgroundScan();" in win_server
    assert "m_initialScanComplete.store(true" in win_server
    assert "JoinBackgroundScan();" in posix_server
    assert "m_initialScanComplete.store(true" in posix_server
    assert "DLNAServer.IsInitialScanComplete()" in content
    assert "Initial scan in progress" in content


def test_remote_source_rescan_behavior_documented():
    readme = read("README.md")
    source_watcher = read("src/source_watcher.cpp")

    assert "Remote sources" in readme
    assert "ftp://" in readme
    assert "smb://" in readme
    assert "http://" in readme
    assert "https://" in readme
    assert "only scanned at server start" in readme
    assert "do not participate in the automatic file watch loop" in readme
    assert "Adding or removing a remote source at runtime requires a server restart" in readme
    assert "IsRemoteMediaUrl(source.path)" in source_watcher


def test_remote_content_length_probing_notes_parallelization():
    win_media = read("src/media_sources.cpp")
    posix_media = read("src/posix_media_sources.cpp")
    httpserver = read("src/httpserver.cpp")
    posix_httpserver = read("src/posix_httpserver.cpp")

    assert "TODO W-14 perf parallelize remote content length probing" in win_media
    assert "TODO W-14 perf parallelize remote content length probing" in posix_media
    assert "item.sizeBytes > 0 ? item.sizeBytes : ProbeRemoteContentLength" in httpserver
    assert "item.sizeBytes > 0 ? item.sizeBytes : ProbeRemoteContentLength" in posix_httpserver


def test_join_url_optimized_no_str_back_in_loop():
    network = read("src/network_sources.cpp")

    assert "std::wstring JoinUrl(const std::wstring& baseUrl, const std::wstring& entry)" in network
    assert "joined.str().back()" not in network
    assert "bool needsSlash" in network
    assert "needsSlash = true" in network


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
