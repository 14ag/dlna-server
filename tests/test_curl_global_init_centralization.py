import os
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _run(binary_path, *args, timeout=60):
    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
    return subprocess.run(
        [str(binary_path), *args],
        capture_output=True, text=True, timeout=timeout, env=env,
    )


class TestCurlGlobalInitCentralization:
    """Task 1 regression guards: curl_global_init must live exactly once
    per platform in Server's constructor, never in function-local statics."""

    def test_curl_global_init_type_is_deleted_from_both_translation_units(self):
        # the whole point of Task 1 is that the CurlGlobalInit struct is
        # deleted so no second translation unit can ever fire
        # curl_global_init from its own function-local static
        for path in ("src/upnp_eventing.cpp", "src/network_sources.cpp"):
            assert "CurlGlobalInit" not in _read(path)

    def test_curl_global_init_appears_exactly_once_per_server_file(self):
        # one synchronous call in Server's constructor per platform
        # guarantees it runs before any GENA notify worker or remote
        # source scan thread can touch libcurl
        for path in ("src/server.cpp", "src/posix_server.cpp"):
            source = _read(path)
            assert source.count("curl_global_init(CURL_GLOBAL_DEFAULT);") == 1

    def test_curl_global_cleanup_pairs_with_init(self):
        # cleanup must mirror init so the process tears libcurl down
        # exactly once on the way out
        for path in ("src/server.cpp", "src/posix_server.cpp"):
            assert "curl_global_cleanup();" in _read(path)

    def test_concurrent_start_smoke_exercises_curl_from_two_subsystems(self, dlna_binary, tmp_path):
        # the first GENA notify and the first remote source probe used to
        # be able to call curl_global_init concurrently from two threads
        # this hook starts the server twice concurrently which drives
        # SSDP GENA setup and source scanning near simultaneously
        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "sample.mp4").write_bytes(b"\x00" * 4096)

        result = _run(dlna_binary, "--source", str(media_dir),
                      "--print-concurrent-start-start-safety")
        assert result.returncode == 0
        assert "is-running=1" in result.stdout
        assert "after-stop-running=0" in result.stdout