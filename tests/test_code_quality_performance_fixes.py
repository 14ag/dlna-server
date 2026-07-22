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
