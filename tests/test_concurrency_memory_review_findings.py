import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


def _read(relative_path):
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def test_task1_watch_thread_mutex_declared():
    header = _read("src/server.h")
    assert "m_watchThreadMutex" in header


def test_task1_watch_thread_guarded_windows():
    src = _read("src/server.cpp")
    start = src.index("void Server::StartWatchMode()")
    stop = src.index("void Server::StopWatchMode()")
    start_body = src[start:stop]
    stop_body = src[stop:stop + 800]
    assert "m_watchThreadMutex" in start_body
    assert "m_watchThreadMutex" in stop_body
    assert "threadToJoin" in stop_body


def test_task2_watch_thread_guarded_posix():
    src = _read("src/posix_server.cpp")
    start = src.index("void Server::StartWatchMode()")
    stop = src.index("void Server::StopWatchMode()")
    start_body = src[start:stop]
    stop_body = src[stop:stop + 800]
    assert "m_watchThreadMutex" in start_body
    assert "m_watchThreadMutex" in stop_body
    assert "threadToJoin" in stop_body


def test_task3_scan_completion_thread_not_detached_windows():
    header = _read("src/server.h")
    src = _read("src/server.cpp")
    assert "m_scanCompletionThread" in header
    start_fn = src.index("bool Server::Start(")
    stop_fn = src.index("bool Server::Rescan(")
    start_body = src[start_fn:stop_fn]
    assert ".detach()" not in start_body
    assert "m_scanCompletionThread = std::thread" in start_body
    stop_body = src[src.index("void Server::Stop()"):]
    assert "m_scanCompletionThread.join()" in stop_body


def test_task4_scan_completion_thread_not_detached_posix():
    src = _read("src/posix_server.cpp")
    start_fn = src.index("bool Server::Start(")
    stop_fn = src.index("bool Server::Rescan(")
    start_body = src[start_fn:stop_fn]
    assert ".detach()" not in start_body
    assert "m_scanCompletionThread = std::thread" in start_body
    stop_body = src[src.index("void Server::Stop()"):]
    assert "m_scanCompletionThread.join()" in stop_body


def test_task5_get_descendants_no_full_catalog_copy():
    src = _read("src/media_sources_common.cpp")
    fn_start = src.index("std::vector<MediaItem> MediaSources::GetDescendants")
    fn_end = src.index("\n}\n", fn_start)
    body = src[fn_start:fn_end]
    assert "items = m_items" not in body
    assert "childrenByParent = m_childrenByParent" not in body
    assert "AppendDescendants(m_items, m_childrenByParent" in body


def test_task6_notify_system_update_id_fast_path():
    header = _read("src/upnp_eventing.h")
    src = _read("src/upnp_eventing.cpp")
    assert "m_subscriberCount" in header
    assert "std::atomic<int> m_lastSystemUpdateId" in header
    fn_start = src.index("void UpnpEventManager::NotifySystemUpdateId")
    fn_end = src.index("\n}\n", fn_start)
    body = src[fn_start:fn_end]
    assert "m_subscriberCount.load" in body
    assert re.search(r"if \(m_subscriberCount\.load\([^)]*\) == 0\)", body)


def test_task6_subscriber_count_updated_at_every_mutation():
    src = _read("src/upnp_eventing.cpp")
    assert src.count("m_subscriberCount.store(") >= 4


def test_task7_catalog_reserve_present():
    src = _read("src/media_sources_common.cpp")
    assert "kInitialCatalogReserve" in src
    fn_start = src.index("void MediaSources::ResetForRescan")
    fn_end = src.index("\n}\n", fn_start)
    assert "m_items.reserve(kInitialCatalogReserve)" in src[fn_start:fn_end]


def test_task8_album_art_stat_calls_not_locked_for_whole_function():
    src = _read("src/media_sources_common.cpp")
    fn_start = src.index("void SetAlbumArtIfExists")
    fn_end = src.index("\n}\n", fn_start)
    body = src[fn_start:fn_end]
    lock_count = len(re.findall(r"lock_guard<std::mutex> lock\(\*state\.mutationMutex", body))
    assert lock_count == 3


def test_task9_source_jobs_not_submitted_to_pool():
    src = _read("src/media_sources_common.cpp")
    fn_start = src.index("void MediaSources::Scan()")
    fn_end = src.index("void MediaSources::AddMediaFile")
    body = src[fn_start:fn_end]
    assert "TaskGroup sourceGroup" not in body
    assert "sourceThreads" in body
    assert "PlaylistScanPool::Get().Submit([this, &job, &sourceGroup]" not in body


def test_task9_dispatcher_still_uses_pool_for_leaf_nodes():
    src = _read("src/media_sources_common.cpp")
    fn_start = src.index("void MediaSources::RunPlaylistDispatcher")
    fn_end = src.index("void MediaSources::ScanOnePlaylistNode")
    body = src[fn_start:fn_end]
    assert "PlaylistScanPool::Get().Submit" in body


def test_task10_bounded_pool_has_optional_queue_depth():
    header = _read("src/bounded_thread_pool.h")
    src = _read("src/bounded_thread_pool.cpp")
    assert "size_t maxQueueDepth = 0" in header
    assert "m_spaceCv" in header
    assert "m_spaceCv.wait" in src
    assert "m_spaceCv.notify_one" in src
    assert "m_spaceCv.notify_all" in src


def test_task10_playlist_pool_uses_bounded_queue():
    header = _read("src/playlist_scan_concurrency.h")
    src = _read("src/playlist_scan_concurrency.cpp")
    assert "kPlaylistScanPoolMaxQueueDepth" in header
    assert "kPlaylistScanPoolMaxQueueDepth" in src


def test_task10_notify_pool_unchanged():
    src = _read("src/upnp_eventing.cpp")
    assert "make_unique<BoundedThreadPool>(kMaxUpnpNotifyWorkers)" in src


def test_task11_media_database_pass_tracking_declared():
    header = _read("src/media_database.h")
    assert "BeginScanPass" in header
    assert "PruneUntouched" in header
    assert "m_touchedThisPass" in header


def test_task11_scan_pass_wired_into_scan():
    src = _read("src/media_sources_common.cpp")
    fn_start = src.index("void MediaSources::Scan()")
    fn_end = src.index("void MediaSources::AddMediaFile")
    body = src[fn_start:fn_end]
    assert "database->BeginScanPass()" in body
    assert "database->PruneUntouched()" in body
    assert body.index("database->BeginScanPass()") < body.index("database->PruneUntouched()")


def test_task12_device_description_config_struct_exists():
    header = _read("src/config.h")
    assert "struct DeviceDescriptionConfig" in header
    assert "GetDeviceDescriptionConfig" in header


def test_task12_get_device_description_xml_uses_lightweight_getter():
    src = _read("src/contentdirectory.cpp")
    fn_start = src.index("std::string ContentDirectory::GetDeviceDescriptionXML")
    fn_end = src.index("\nstd::string ContentDirectory::GetContentDirectoryXML")
    body = src[fn_start:fn_end]
    assert "AppConfig.GetDeviceDescriptionConfig()" in body
    assert "AppConfig.Snapshot()" not in body


def test_task13_scoped_handle_not_copyable():
    src = _read("src/httpserver.cpp")
    fn_start = src.index("struct ScopedHandle")
    fn_end = src.index("};", fn_start)
    body = src[fn_start:fn_end]
    assert "ScopedHandle(const ScopedHandle&) = delete;" in body
    assert "ScopedHandle& operator=(const ScopedHandle&) = delete;" in body


def test_task14_get_or_create_stable_id_locked_helper_exists():
    header = _read("src/media_database.h")
    src = _read("src/media_database.cpp")
    assert "GetOrCreateStableIdLocked" in header
    assert src.count("MediaDatabase::GetOrCreateStableIdLocked") == 1
    assert src.count("GetOrCreateStableIdLocked(canonicalKey)") == 2


def test_task14_record_scan_error_single_lock():
    src = _read("src/media_database.cpp")
    fn_start = src.index("void MediaDatabase::RecordScanError")
    fn_end = src.index("\n}\n", fn_start)
    body = src[fn_start:fn_end]
    assert body.count("std::lock_guard<std::mutex>") == 1
    assert "GetOrCreateStableId(canonicalKey);   // locks and releases internally" not in body


def test_task16_ssdp_target_struct_moved_to_ssdp_header():
    ssdp_header = _read("src/ssdp.h")
    ssdp_common_header = _read("src/ssdp_common.h")
    assert "struct SSDPTarget" in ssdp_header
    assert "struct SSDPTarget" not in ssdp_common_header
    assert "m_targets" in ssdp_header


def test_task16_targets_built_once_per_start_windows():
    src = _read("src/ssdp.cpp")
    assert src.count("BuildAdvertisedTargets(m_uuidStr)") == 1
    fn_start = src.index("void SSDP::SendNotifyRound")
    fn_end = src.index("void SSDP::SendNotifyBurst")
    assert "m_targets" in src[fn_start:fn_end]


def test_task16_targets_built_once_per_start_posix():
    src = _read("src/posix_ssdp.cpp")
    assert src.count("BuildAdvertisedTargets(m_uuidStr)") == 1
    fn_start = src.index("void SSDP::SendNotifyRound")
    fn_end = src.index("void SSDP::SendNotifyBurst")
    assert "m_targets" in src[fn_start:fn_end]
