import time
from pathlib import Path

import pytest


class TestStartReturnsBeforeScanCompletes:
    def test_start_returns_before_scan_completes(
            self, slow_playlist_source, running_server):
        """Server TCP port opens and GetSystemUpdateID succeeds before
        the full playlist scan (20 nested x 100 ms delay) would complete
        serially (~2 s on the HTTP fetches alone)."""
        uid = running_server.soap_get_system_update_id()
        assert uid > 0, (
            "GetSystemUpdateID should return a positive value "
            "even while scan is still in progress")


class TestBrowseReturnsPartialResultsDuringScan:
    def test_browse_returns_partial_results_during_scan(
            self, slow_playlist_source, running_server):
        """Browse polling returns non-decreasing NumberReturned and
        never returns UPnP error 710."""
        previous = -1
        final = None
        for _ in range(60):
            result = running_server.soap_browse(object_id="0")
            if result.get("errorCode"):
                err = result.get("errorCode")
                assert err != 710, (
                    f"UPnP error 710 during scan: "
                    f"{result.get('errorDescription')}")
                break
            n = result["NumberReturned"]
            assert n >= previous, (
                f"NumberReturned decreased: {previous} -> {n}")
            previous = n
            if n > 0 and result["TotalMatches"] == n:
                final = n
                break
            time.sleep(0.5)
        assert final is not None, (
            "Browse never reached final count within 60 polls")


class TestSsdpAliveFiresBeforeScanCompletes:
    def test_ssdp_alive_fires_before_scan_completes(
            self, slow_playlist_source, media_source_dir, dlna_binary):
        from tests.fixtures.ssdp_listener import SsdpListener

        listener = SsdpListener(timeout=5.0)
        listener.start()
        if not listener.has_multicast:
            pytest.skip("SSDP multicast join requires admin on Windows")

        from tests.fixtures.make_nested_playlist_tree import (
            make_nested_playlist_tree)
        result = make_nested_playlist_tree(
            root_dir=media_source_dir, num_nested=20, delay_ms=100)
        with result["serve"]():
            from tests.conftest import _free_port, _launch_server, \
                _teardown_server
            binary_path = dlna_binary
            port = _free_port()
            proc, connected, old_config, config_ini = _launch_server(
                binary_path, port, media_source_dir)
            try:
                assert connected, "Server did not start"
                alive = listener.wait_for_alive(timeout=5.0)
                if not alive:
                    binary_dir = Path(binary_path).parent
                    debug_log = binary_dir / "debug.log"
                    if debug_log.exists():
                        content = debug_log.read_text(encoding="utf-8-sig")
                        alive = "nts=ssdp:alive" in content
                assert alive, (
                    "ssdp:alive NOTIFY not observed within 5s "
                    "(multicast listener timed out and debug.log "
                    "had no SSDP notify entries either)")
            finally:
                _teardown_server(proc, old_config, config_ini)
        listener.stop()
