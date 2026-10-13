from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_async_gena_event_manager_exists_and_queues_system_update_notifications():
    header = read("src/upnp_eventing.h")
    source = read("src/upnp_eventing.cpp")

    for token in (
        "class UpnpEventManager",
        "RegisterSubscription",
        "RenewSubscription",
        "RemoveSubscription",
        "ClearSubscriptions",
        "NotifySystemUpdateId",
        "std::condition_variable",
        "std::deque<NotifyJob>",
        "std::thread m_worker",
        "SendNotifyJob",
        "SystemUpdateID",
        "NTS: upnp:propchange",
    ):
        assert token in header + source


def test_event_subscribe_unsubscribe_routes_are_shared_by_windows_and_posix_http():
    windows = read("src/httpserver.cpp")
    posix = read("src/posix_httpserver.cpp")

    for source in (windows, posix):
        assert '#include "upnp_eventing.h"' in source
        assert "AppEvents.HandleEventSubscription(method, path, req)" in source
        assert "AppEvents.ClearSubscriptions()" in source

    assert "std::string EventSubscriptionResponse" not in windows
    assert "std::string EventSubscriptionResponse" not in posix


def test_server_watch_mode_detects_local_source_changes_and_rescans_without_restart():
    header = read("src/server.h")
    windows = read("src/server.cpp")
    posix = read("src/posix_server.cpp")
    watcher_header = read("src/source_watcher.h")
    watcher_source = read("src/source_watcher.cpp")
    cmake = read("CMakeLists.txt")

    for token in (
        "StartWatchMode",
        "StopWatchMode",
        "WatchLoop",
        "std::condition_variable",
        "std::atomic<bool> m_stopWatch",
        "std::thread m_watchThread",
    ):
        assert token in header

    common = read("src/server_common.cpp")
    for source in (windows, posix):
        assert '#include "source_watcher.h"' in source
        assert "StartWatchMode()" in source
        assert "StartBackgroundScan()" in source
    for token in ("StopWatchMode()", "MediaSourcesHaveChanged(cfg, signature)"):
        assert token in common

    for token in (
        "ComputeMediaSourceSignature",
        "MediaSourcesHaveChanged",
        "std::filesystem::recursive_directory_iterator",
        "kMaxWatchDepth",
        "IsRemoteMediaUrl",
    ):
        assert token in watcher_header + watcher_source

    assert "src/source_watcher.cpp" in cmake
    assert "src/source_watcher.h" in cmake


def test_watch_loop_uses_fresh_config_snapshot_each_poll():
    header = read("src/server.h")
    common = read("src/server_common.cpp")
    assert "void WatchLoop()" in header
    assert "WatchLoop(ConfigSnapshot cfg)" not in header
    assert "void Server::WatchLoop()" in common
    assert "ConfigSnapshot cfg = AppConfig.Snapshot()" in common
    assert "MediaSourcesHaveChanged(cfg, signature)" in common
    assert "WatchLoop(ConfigSnapshot cfg)" not in common


def test_source_watcher_does_not_ignore_entries_after_a_fixed_cap():
    watcher_source = read("src/source_watcher.cpp")
    watcher_header = read("src/source_watcher.h")

    assert "kMaxWatchedEntries" not in watcher_source + watcher_header
    assert "count < kMaxWatchedEntries" not in watcher_source
    assert "if (count >= kMaxWatchedEntries)" not in watcher_source
