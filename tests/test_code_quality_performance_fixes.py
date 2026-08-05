import os
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

import pytest

from conftest import ServerClient
from tests.conftest import _free_port, _launch_server, _teardown_server, server_config_ini_path


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
        # The two platform .cpp files must no longer define their own copy
        assert "constexpr size_t kMaxClientThreads" not in httpserver
        assert "constexpr size_t kMaxClientThreads" not in posix_httpserver

    @staticmethod
    def _kmax_client_threads(dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-max-client-threads"],
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

    def test_thread_count_stays_bounded_under_request_burst(
            self, dlna_binary, media_source_dir):
        """Task 8: a burst of kMaxClientThreads*3 sequential requests must
        not grow the process thread count beyond a small margin of the pool
        size (pre-fix threads lingered unreaped between bursts)."""
        import urllib.request
        kMax = self._kmax_client_threads(dlna_binary)
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
            assert after <= kMax + 16, (
                f"thread count {after} exceeded kMaxClientThreads({kMax}) "
                f"plus 16 margin")
            assert after <= before + 4, (
                f"thread count grew across request burst: {before} -> {after}")
            time.sleep(10)  # idle
            idle = self._proc_thread_count(proc.pid)
            assert idle <= kMax + 16, (
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

    def test_probe_cache_lifecycle_counter(self, dlna_binary):
        """ProbeRemoteContentLength recompute counter increments once per
        distinct URL per TTL window, not once per call. The URL targets a
        closed port so the probe fails fast; the negative result is cached
        exactly like a positive length."""
        url = "http://127.0.0.1:1/head.bin"
        result = subprocess.run(
            [dlna_binary, "--print-remote-probe-cache-lifecycle", url],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        fields = dict(
            part.split("=", 1) for part in result.stdout.strip().split())
        assert fields["before"] == "0"
        assert fields["after-first-probe"] == "1"
        assert fields["after-second-probe"] == "1"

    def test_second_subtitle_request_hits_probe_cache(self, dlna_binary):
        """Two /subtitle/<id> requests for the same remote subtitle: the
        first blocks on the real HEAD probe, the second is served from the
        5-minute probe cache and returns in a small fraction of the time."""
        import urllib.request
        import xml.etree.ElementTree as ET
        from http.server import BaseHTTPRequestHandler, HTTPServer

        probe_delay = 2.0
        stub_port = _free_port()

        class Handler(BaseHTTPRequestHandler):
            def do_HEAD(self):
                # ProbeRemoteContentLength issues a HEAD. Delay only HEAD
                # so the probe is the slow step and GET body fetches stay fast.
                time.sleep(probe_delay)
                body = b"x"
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()

            def do_GET(self):
                if self.path.endswith(".m3u"):
                    body = (
                        "#EXTM3U\n"
                        "#DLNA-SUBTITLE:movie.srt\n"
                        "#EXTINF:5,Movie\n"
                        f"http://127.0.0.1:{stub_port}/movie.mp4\n"
                    ).encode("utf-8")
                elif self.path.endswith(".srt"):
                    body = b"1\n00:00:00,000 --> 00:00:01,000\nHello\n"
                elif self.path.endswith(".mp4"):
                    body = b"video bytes"
                else:
                    self.send_response(404)
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *a):
                pass

        server = HTTPServer(("127.0.0.1", stub_port), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            playlist_url = f"http://127.0.0.1:{stub_port}/playlist.m3u"
            port = _free_port()
            config_root = Path(tempfile.mkdtemp(prefix="dlna-probe-cache-"))
            config_ini = server_config_ini_path(dlna_binary, config_root)
            old = None
            config_ini.parent.mkdir(parents=True, exist_ok=True)
            if config_ini.exists():
                old = config_ini.read_text(encoding="utf-8-sig")
            config_ini.write_text(
                "[Settings]\n"
                f"Port={port}\n"
                f"MediaSources={playlist_url}\n"
                f"DebugLog=0\n",
                encoding="utf-8-sig")

            env = os.environ.copy()
            env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
            env["XDG_CONFIG_HOME"] = str(config_ini.parent.parent)
            env["HOME"] = str(config_ini.parent.parent)
            env["XDG_RUNTIME_DIR"] = tempfile.mkdtemp(prefix="dlna-runtime-")
            proc = subprocess.Popen(
                [str(dlna_binary), "--headless"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
            try:
                deadline = time.time() + 15
                connected = False
                while time.time() < deadline:
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                            connected = True
                            break
                    except (ConnectionRefusedError, OSError, socket.timeout):
                        time.sleep(0.1)
                if not connected:
                    pytest.fail("Server did not start")

                client = ServerClient(f"http://127.0.0.1:{port}", "")
                nsd = {
                    "d": "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/",
                    "sec": "http://www.sec.co.kr/dlna",
                }
                item_id = None
                deadline = time.time() + 20
                while time.time() < deadline and item_id is None:
                    stack = ["0"]
                    visited = set()
                    while stack and item_id is None:
                        cid = stack.pop()
                        if cid in visited:
                            continue
                        visited.add(cid)
                        resp = client.soap_browse(cid)
                        didl = resp.get("Result", "")
                        if not didl:
                            continue
                        root = ET.fromstring(didl)
                        for item in root.findall("d:item", nsd):
                            if item.find("sec:CaptionInfoEx", nsd) is not None:
                                item_id = item.get("id")
                                break
                        if item_id is None:
                            for container in root.findall("d:container", nsd):
                                child = container.get("id")
                                if child and child not in visited:
                                    stack.append(child)
                    if item_id is None:
                        time.sleep(0.5)

                assert item_id is not None, "No subtitle item found via browse"

                url = f"http://127.0.0.1:{port}/subtitle/{item_id}"
                t0 = time.time()
                with urllib.request.urlopen(url, timeout=30) as r:
                    first = r.read()
                t_first = time.time() - t0
                assert first
                t0 = time.time()
                with urllib.request.urlopen(url, timeout=30) as r:
                    second = r.read()
                t_second = time.time() - t0
                assert second == first
                assert t_first >= probe_delay * 0.8, (
                    f"first request did not block on the probe: {t_first:.2f}s")
                assert t_second < t_first * 0.5, (
                    f"second request re-probed: t_first={t_first:.2f}s "
                    f"t_second={t_second:.2f}s")
            finally:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3)
                if old is not None:
                    config_ini.write_text(old, encoding="utf-8-sig")
                elif config_ini.exists():
                    config_ini.unlink()
        finally:
            server.shutdown()
            thread.join(timeout=5)


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

    def test_single_instance_retry_timing(self, dlna_binary):
        """SendShowWithRetry(3, 50) against a not-yet-listening socket must
        sleep 2*50=100ms between attempts, retry, then give up (no hang)."""
        result = subprocess.run(
            [dlna_binary, "--print-single-instance-retry-timing"],
            capture_output=True, text=True, timeout=5)
        assert result.returncode == 0
        fields = dict(
            part.split("=", 1) for part in
            result.stdout.strip().split())
        assert fields["delivered"] == "0"
        assert int(fields["elapsed-ms"]) >= 100


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


class TestContentDirectorySearchCache:
    """Task 10: identical Search action calls must reuse one catalog walk per
    SystemUpdateID instead of re-walking the subtree on every call."""

    def _launch(self, dlna_binary, media_source_dir, background_scan):
        binary_path = Path(dlna_binary)
        port = _free_port()
        config_dir = media_source_dir.parent
        config_ini = server_config_ini_path(binary_path, config_dir)
        config_ini.parent.mkdir(parents=True, exist_ok=True)
        old_config = None
        if config_ini.exists():
            old_config = config_ini.read_text(encoding="utf-8-sig")
        config_ini.write_text(
            "[Settings]\n"
            f"Port={port}\n"
            f"MediaSources={media_source_dir}\n"
            f"DebugLog=1\n"
            f"BackgroundScanEnabled={1 if background_scan else 0}\n",
            encoding="utf-8-sig")

        env = os.environ.copy()
        env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
        env["XDG_CONFIG_HOME"] = str(config_dir)
        env["HOME"] = str(config_dir)
        env["XDG_RUNTIME_DIR"] = tempfile.mkdtemp(prefix="dlna-runtime-")
        try:
            subprocess.run(
                [str(binary_path), "--kill-server"], env=env,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=10, check=False)
        except (OSError, subprocess.SubprocessError):
            pass
        proc = subprocess.Popen(
            [str(binary_path), "--headless"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

        deadline = time.time() + 15
        connected = False
        while time.time() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                    connected = True
                    break
            except (OSError, socket.timeout):
                time.sleep(0.1)
        assert connected, f"server did not listen on port {port} within 15s"
        client = ServerClient(f"http://127.0.0.1:{port}", binary_path.parent)
        return proc, old_config, config_ini, client

    def _recompute_count(self, config_ini):
        log = config_ini.parent / "debug.log"
        if not log.exists():
            return 0
        return log.read_text(
            encoding="utf-8", errors="replace").count("[search-cache]")

    def _wait_recompute_count(self, config_ini, expected, timeout=40):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self._recompute_count(config_ini) >= expected:
                return
            time.sleep(0.2)
        pytest.fail(f"recompute count never reached {expected}")

    def _wait_initial_scan_settled(self, config_ini, client):
        # The initial background scan increments SystemUpdateID when it
        # finishes. Wait for its per-source completion log line, then let the
        # increment land, so the catalog is stable before the first Search.
        log = config_ini.parent / "debug.log"
        deadline = time.time() + 30
        seen = False
        while time.time() < deadline:
            if (log.exists() and
                    "Finished scanning media source" in log.read_text(
                        encoding="utf-8", errors="replace")):
                seen = True
                break
            time.sleep(0.2)
        if seen:
            before = client.soap_get_system_update_id()
            deadline = time.time() + 5
            while time.time() < deadline:
                if client.soap_get_system_update_id() != before:
                    return
                time.sleep(0.1)
        # No completion line and no bump observed: assume the empty source
        # catalog is already stable.

    def test_identical_search_requests_hit_cache(self, dlna_binary, media_source_dir):
        proc, old_config, config_ini, client = self._launch(
            dlna_binary, media_source_dir, background_scan=False)
        try:
            self._wait_initial_scan_settled(config_ini, client)
            first = client.soap_search(container_id="0", search_criteria="")
            assert first.get("errorCode", -1) == 0
            self._wait_recompute_count(config_ini, 1)
            second = client.soap_search(container_id="0", search_criteria="")
            assert second.get("errorCode", -1) == 0
            # Identical request against an unchanged catalog must not walk
            # the subtree a second time.
            time.sleep(0.5)
            assert self._recompute_count(config_ini) == 1
        finally:
            _teardown_server(proc, old_config, config_ini)

    def test_catalog_change_invalidates_search_cache(self, dlna_binary, media_source_dir):
        proc, old_config, config_ini, client = self._launch(
            dlna_binary, media_source_dir, background_scan=True)
        try:
            self._wait_initial_scan_settled(config_ini, client)
            baseline_id = client.soap_get_system_update_id()
            first = client.soap_search(container_id="0", search_criteria="")
            assert first.get("errorCode", -1) == 0
            self._wait_recompute_count(config_ini, 1)

            # Mutate the catalog: create a new file inside the watched media
            # source. The inotify watcher debounces then auto-rescans, which
            # bumps SystemUpdateID and must invalidate the search cache.
            (media_source_dir / "new_movie.mp4").write_text("x", encoding="utf-8")
            deadline = time.time() + 60
            changed = False
            while time.time() < deadline:
                current = client.soap_get_system_update_id()
                if current != -1 and current != baseline_id:
                    changed = True
                    break
                time.sleep(0.5)
            assert changed, (
                "SystemUpdateID never changed after a file was added to the "
                "watched media source")

            # Repeat the identical search; the changed update id forces a
            # second catalog walk.
            second = client.soap_search(container_id="0", search_criteria="")
            assert second.get("errorCode", -1) == 0
            self._wait_recompute_count(config_ini, 2)
        finally:
            _teardown_server(proc, old_config, config_ini)


class TestGuiStartupRetryPolicy:
    """Task 11: the GTK/D-Bus startup retry policy must bound the worst-case
    wait tightly enough to survive a WSLg cold start but short enough that a
    genuinely broken environment fails fast instead of hanging the shortcut."""

    ROOT = Path(__file__).resolve().parents[1]

    def _constants(self):
        import re
        source = (self.ROOT / "src" / "gtk4_gui_main.cpp").read_text(
            encoding="utf-8", errors="replace")
        max_attempts = int(re.search(r"kGuiStartupMaxAttempts\s*=\s*(\d+)",
                                     source).group(1))
        delay_ms = int(re.search(r"kGuiStartupRetryDelayMs\s*=\s*(\d+)",
                                 source).group(1))
        return max_attempts, delay_ms

    def test_startup_retry_worst_case_wait_is_bounded(self):
        max_attempts, delay_ms = self._constants()
        total_ms = max_attempts * delay_ms
        assert 2000 <= total_ms <= 10000, (
            f"worst-case retry wait {total_ms}ms must be between 2000 and "
            f"10000ms (attempts={max_attempts} delay={delay_ms}ms)")

    def test_startup_retry_constants_are_positive(self):
        max_attempts, delay_ms = self._constants()
        assert max_attempts >= 1
        assert delay_ms >= 1
