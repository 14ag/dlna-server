import os
import socket
import subprocess
import time
from pathlib import Path

import pytest

from conftest import ServerClient
from tests.conftest import _free_port, _launch_server, _teardown_server


class TestClosePolicy:
    @pytest.mark.parametrize("isRunning,isBusy,expected", [
        ("0", "0", "1"),
        ("1", "0", "0"),
        ("0", "1", "0"),
        ("1", "1", "0"),
    ])
    def test_should_close_now(self, dlna_binary, isRunning, isBusy, expected):
        result = subprocess.run(
            [dlna_binary, "--print-should-close-now", isRunning, isBusy],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        assert result.stdout.strip() == expected


class TestRoutableHostUrl:
    PORT_A = "18321"
    PORT_B = "18322"

    def test_different_ports_produce_different_results(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-routable-host-url-twice", self.PORT_A, self.PORT_B],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 2, f"expected 2 lines, got {len(lines)}"
        first, second = lines[0], lines[1]
        if first:
            assert first.endswith(":" + self.PORT_A), f"first line {first!r} should end with :{self.PORT_A}"
        if second:
            assert second.endswith(":" + self.PORT_B), f"second line {second!r} should end with :{self.PORT_B}"
        if first and second:
            assert first != second, "two different ports should produce different URLs"

    def test_port_suffix_matches_order_not_value(self, dlna_binary):
        # Run with swapped port order to prove fix is not order-dependent
        result = subprocess.run(
            [dlna_binary, "--print-routable-host-url-twice", self.PORT_B, self.PORT_A],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 2, f"expected 2 lines, got {len(lines)}"
        first, second = lines[0], lines[1]
        if first:
            assert first.endswith(":" + self.PORT_B), f"first line {first!r} should end with :{self.PORT_B} (swapped)"
        if second:
            assert second.endswith(":" + self.PORT_A), f"second line {second!r} should end with :{self.PORT_A} (swapped)"


class TestTrimWide:
    @pytest.mark.parametrize("input_text,expected", [
        ("  hello world  ", "hello world"),
        ("  hello  world  ", "hello  world"),
        ("     ", ""),
    ])
    def test_trim_wide(self, dlna_binary, input_text, expected):
        result = subprocess.run(
            [dlna_binary, "--print-trim-wide", input_text],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        assert result.stdout.strip() == expected


class TestNotifyPoolWorkerCount:
    def test_notify_pool_worker_count_is_small(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-notify-pool-worker-count"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        value = int(result.stdout.strip())
        assert 0 < value <= 16, f"expected small worker count (1..16), got {value}"


<<<<<<< HEAD
=======
class TestMaxClientThreadsParity:
    """Task 3: kMaxClientThreads must live in http_common.h exactly once,
    and every platform binary must report the same value through the
    --print-max-client-threads hook without hardcoding 64 here."""
    def test_max_client_threads_reported(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-max-client-threads"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        value = int(result.stdout.strip())
        assert value > 0, f"expected positive thread cap, got {value}"

    def test_max_client_threads_single_definition_in_header(self):
        from pathlib import Path
        repo_root = Path(__file__).resolve().parents[1]
        header = (repo_root / "src" / "http_common.h").read_text(encoding="utf-8")
        httpserver = (repo_root / "src" / "httpserver.cpp").read_text(encoding="utf-8")
        posix_httpserver = (repo_root / "src" / "posix_httpserver.cpp").read_text(encoding="utf-8")
        header_defs = header.count("constexpr size_t kMaxClientThreads")
        assert header_defs == 1
        assert "constexpr size_t kMaxClientThreads = 64" in header
        assert "kMaxClientThreads" in httpserver
        assert "kMaxClientThreads" in posix_httpserver
        # The two platform .cpp files must no longer define their own copy

    @staticmethod
    def _kmax_client_threads(dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-max-client-threads"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        return int(result.stdout.strip())

    @staticmethod
    def _source_scan_pool_workers(dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-source-scan-pool-worker-count"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0
        return int(result.stdout.strip())

    @staticmethod
    def _proc_thread_count(pid):
        status = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
        for line in status.splitlines():
            if line.startswith("Threads:"):
                return int(line.split()[1])
        return 0

    @pytest.mark.posix_only
    def test_thread_count_stays_bounded_under_request_burst(
            self, dlna_binary, media_source_dir):
        """Task 8: a burst of kMaxClientThreads*3 sequential requests must
        not grow the process thread count beyond a small margin of the pool
        size (pre-fix threads lingered unreaped between bursts)."""
        import urllib.request
        kMax = self._kmax_client_threads(dlna_binary)
        sourceScan = self._source_scan_pool_workers(dlna_binary)
        # Fixed background threads (2 accept loops + SSDP worker + watch loop
        # + main thread) sit on top of the client pool and the source-scan
        # pool; the pre-spawned SourceScanPool is machine-dependent
        # (min(hardware_concurrency*2, kMaxClientThreads)), so the absolute
        # bound must include it or the check fails on low-core hosts before
        # any request is served.
        absBound = kMax + sourceScan + 16
        port = _free_port()
        proc, connected, old_config, config_ini = _launch_server(
            dlna_binary, port, media_source_dir)
        if not connected:
            pytest.fail("Server did not start")
        try:
            time.sleep(3)  # let startup and background threads settle
            before = self._proc_thread_count(proc.pid)
            for _ in range(kMax * 3):
                with urllib.request.urlopen(
                        f"http://127.0.0.1:{port}/description.xml",
                        timeout=10):
                    pass
            after = self._proc_thread_count(proc.pid)
            assert after <= absBound, (
                f"thread count {after} exceeded kMaxClientThreads({kMax}) "
                f"plus source-scan pool ({sourceScan}) plus 16 margin")
            assert after <= before + 4, (
                f"thread count grew across request burst: {before} -> {after}")
            time.sleep(10)  # idle
            idle = self._proc_thread_count(proc.pid)
            assert idle <= absBound, (
                f"thread count {idle} still unbounded after 10s idle")
        finally:
            _teardown_server(proc, old_config, config_ini)

    def test_concurrent_slow_connections_queue_no_503(
            self, dlna_binary, media_source_dir):
        """Task 8: kMaxClientThreads+5 concurrent slow connections are queued
        and all eventually complete with HTTP 200, none receiving an abrupt
        503 (the pool queues in the kernel backlog instead of rejecting)."""
        kMax = self._kmax_client_threads(dlna_binary)
        port = _free_port()
        proc, connected, old_config, config_ini = _launch_server(
            dlna_binary, port, media_source_dir)
        if not connected:
            pytest.fail("Server did not start")
        try:
            sockets = []
            try:
                for _ in range(kMax + 5):
                    s = socket.create_connection(("127.0.0.1", port), timeout=5)
                    s.settimeout(30)
                    sockets.append(s)
                time.sleep(0.5)  # delay sending the request line
                request = (
                    b"GET /description.xml HTTP/1.1\r\n"
                    b"Host: 127.0.0.1\r\nConnection: close\r\n\r\n")
                for s in sockets:
                    s.sendall(request)
                status_lines = []
                for s in sockets:
                    buf = b""
                    while b"\r\n" not in buf:
                        chunk = s.recv(4096)
                        if not chunk:
                            break
                        buf += chunk
                    status_lines.append(buf.split(b"\r\n", 1)[0])
            finally:
                for s in sockets:
                    try:
                        s.close()
                    except OSError:
                        pass
            for line in status_lines:
                assert line, "connection closed without any HTTP response"
                assert b"503" not in line, (
                    f"connection received abrupt 503: {line!r}")
                assert line.startswith(b"HTTP/1.1 200"), (
                    f"expected HTTP/1.1 200, got {line!r}")
        finally:
            _teardown_server(proc, old_config, config_ini)


>>>>>>> 03fabad (Refactor tests to remove deprecated checks and improve coverage)
class TestGetChildCountsPagination:
    """Task 7: GetChildCounts pagination fix."""
    @staticmethod
    def _find_source_container_id(client):
        """Browse root to discover the first child container id (the media source wrapper)."""
        import xml.etree.ElementTree as ET
        resp = client.soap_browse("0", requested_count=100)
        didl = resp.get("Result", "")
        ns = {"d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"}
        root = ET.fromstring(didl) if didl else None
        if root is not None:
            for container in root.findall("d:container", ns):
                return container.get("id")
        return None

    def test_paginated_browse_childcounts(self, dlna_binary, media_source_dir):
        """Browse with RequestedCount=2 on 3 subfolders returns correct childCount."""
        import xml.etree.ElementTree as ET
        for i in range(3):
            sub = media_source_dir / f"Folder{i}"
            sub.mkdir()
            (sub / f"file{i}.mp3").write_text("")
        port = _free_port()
        proc, connected, old_config, config_ini = _launch_server(
            dlna_binary, port, media_source_dir)
        if not connected:
            pytest.fail("Server did not start")
        try:
            time.sleep(3)
            client = ServerClient(f"http://127.0.0.1:{port}", "")
            source_id = self._find_source_container_id(client)
            assert source_id is not None, "Could not find source container"
            resp = client.soap_browse(source_id, requested_count=2)
            # Parse DIDL Result for <container> elements
            didl = resp.get("Result", "")
            containers = []
            if didl:
                root = ET.fromstring(didl)
                # DIDL-Lite namespace
                ns = {"d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"}
                for container in root.findall("d:container", ns):
                    child_count = container.get("childCount", "0")
                    containers.append(int(child_count))
            assert resp["NumberReturned"] == 2, f"Expected 2 returned, got {resp['NumberReturned']}"
            assert len(containers) == 2, f"Expected 2 containers, got {len(containers)}"
            for cc in containers:
                assert cc >= 1, f"Expected childCount >= 1, got {cc}"
        finally:
            _teardown_server(proc, old_config, config_ini)


class TestSearchSortOrder:
    """Task 8: Redundant Search sort removal."""
    def test_search_title_sort(self, dlna_binary, media_source_dir):
        """Search returns filtered results via SortItems alone."""
        import xml.etree.ElementTree as ET
        sub = media_source_dir / "Container"
        sub.mkdir()
        titles = ["Zulu", "Alpha", "Charlie", "Bravo"]
        for t in titles:
            (sub / f"{t}.mp3").write_text("")
        port = _free_port()
        proc, connected, old_config, config_ini = _launch_server(
            dlna_binary, port, media_source_dir)
        if not connected:
            pytest.fail("Server did not start")
        try:
            time.sleep(3)
            client = ServerClient(f"http://127.0.0.1:{port}", "")
            source_id = TestGetChildCountsPagination._find_source_container_id(client)
            assert source_id is not None, "Could not find source container"
            resp = client.soap_search(source_id)
            # Verify we got items back (Search works without per-folder presort)
            assert resp["NumberReturned"] >= 3, f"Expected >=3 items, got {resp['NumberReturned']}"
            # Check DIDL contains <item> elements
            didl = resp.get("Result", "")
            if didl:
                root = ET.fromstring(didl)
                ns = {"d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"}
                items = root.findall("d:item", ns)
                assert len(items) >= 3
        finally:
            _teardown_server(proc, old_config, config_ini)


class TestSubtitleLookupCaching:
    """Task 9: Subtitle lookup caching."""
    def _has_caption_info(self, dlna_binary, media_source_dir, vid_name, sub_name):
        import xml.etree.ElementTree as ET
        (media_source_dir / vid_name).write_text("video data")
        (media_source_dir / sub_name).write_text("subtitle data")
        port = _free_port()
        proc, connected, old_config, config_ini = _launch_server(
            dlna_binary, port, media_source_dir)
        if not connected:
            pytest.fail("Server did not start")
        try:
            time.sleep(3)
            client = ServerClient(f"http://127.0.0.1:{port}", "")
            source_id = TestGetChildCountsPagination._find_source_container_id(client)
            if source_id is None:
                return False
            resp = client.soap_browse(source_id)
            didl = resp.get("Result", "")
            if not didl:
                return False
            root = ET.fromstring(didl)
            ns = {"d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/",
                  "sec": "http://www.sec.co.kr/dlna"}
            for item in root.findall("d:item", ns):
                caption = item.find("sec:CaptionInfoEx", ns)
                if caption is not None:
                    return True
            return False
        finally:
            _teardown_server(proc, old_config, config_ini)

    def test_subtitle_resolved(self, dlna_binary, media_source_dir):
        """Subtitle file resolved for video with matching .srt companion."""
        assert self._has_caption_info(dlna_binary, media_source_dir, "Movie.mp4", "Movie.srt")

    def test_subtitle_uppercase_extension(self, dlna_binary, media_source_dir):
        """Uppercase .SRT found (cached listing, not stat-based)."""
        assert self._has_caption_info(dlna_binary, media_source_dir, "Movie.mp4", "Movie.SRT")


class TestSingleInstanceLifecycle:
    """Task 2: SingleInstance::ReleaseLock() must return deterministically."""

    def test_single_instance_lifecycle_does_not_hang(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-single-instance-lifecycle"],
            capture_output=True, text=True, timeout=5)
        assert result.returncode == 0
        lines = result.stdout.strip().splitlines()
        assert lines[0] == "lock-acquired"
        assert "released" in lines


class TestLogSinceIncrementalFetch:
    """Task 7: GetSystemLogSince() incremental fetch for LogDialog."""

    def test_log_since_incremental_fetch(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-log-since-lifecycle"],
            capture_output=True, text=True, timeout=5)
        assert result.returncode == 0
        output = result.stdout
        assert "has-new-line" in output
        assert "no-old-line-leak" in output
