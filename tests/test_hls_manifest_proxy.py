import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


# ---------------------------------------------------------------------------
# Source-contract tests (fast, no server needed)
# Verify Phase 3 HLS manifest URI rewrite design:
#   - isHlsManifest variable removed from both httpserver files
#   - HLS items handled by early return branch before remote/local paths
#   - FetchHlsManifestForServing + BuildHlsContentFeatures used in HLS branch
#   - Remote/local branches no longer have HLS ternaries
#   - Samsung spoof still present and unchanged
# ---------------------------------------------------------------------------

class HlsManifestProxyFixSourceTests(unittest.TestCase):
    def _read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    # --- Task 1: Windows httpserver checks ---

    def test_windows_hls_early_return_branch(self):
        src = self._read("src/httpserver.cpp")
        # HLS items are handled before IsRemoteMediaUrl check
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        idx_remote = src.find("if (IsRemoteMediaUrl(item.path))")
        self.assertGreater(idx_hls, 0, "HLS mime check not found")
        self.assertGreater(idx_remote, 0, "IsRemoteMediaUrl not found")
        self.assertLess(idx_hls, idx_remote,
                        "HLS branch must appear before IsRemoteMediaUrl")

    def test_windows_isHlsManifest_removed(self):
        src = self._read("src/httpserver.cpp")
        self.assertNotIn("isHlsManifest", src,
                         "isHlsManifest must not exist in httpserver.cpp")

    def test_windows_hls_branch_uses_fetch_and_features(self):
        src = self._read("src/httpserver.cpp")
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        self.assertGreater(idx_hls, 0)
        region = src[idx_hls:idx_hls + 1500]
        self.assertIn("HlsManifestFetchResult", region)
        self.assertIn("FetchHlsManifestForServing", region)
        self.assertIn("BuildHlsContentFeatures()", region)
        self.assertIn("<< manifest.text.size()", region)
        self.assertIn('Accept-Ranges: none', region)

    def test_windows_non_hls_accept_ranges_bytes_preserved(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn('Accept-Ranges: bytes', src)

    def test_windows_spoofSamsung_unchanged(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn("Content-Length: 1073741824", src)
        self.assertIn("Accept-Ranges: none", src)

    # --- Task 2: POSIX httpserver checks ---

    def test_posix_hls_early_return_branch(self):
        src = self._read("src/posix_httpserver.cpp")
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        idx_remote = src.find("if (IsRemoteMediaUrl(item.path))")
        self.assertGreater(idx_hls, 0, "HLS mime check not found")
        self.assertGreater(idx_remote, 0, "IsRemoteMediaUrl not found")
        self.assertLess(idx_hls, idx_remote,
                        "HLS branch must appear before IsRemoteMediaUrl")

    def test_posix_isHlsManifest_removed(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertNotIn("isHlsManifest", src,
                         "isHlsManifest must not exist in posix_httpserver.cpp")

    def test_posix_hls_branch_uses_fetch_and_features(self):
        src = self._read("src/posix_httpserver.cpp")
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        self.assertGreater(idx_hls, 0)
        region = src[idx_hls:idx_hls + 1500]
        self.assertIn("HlsManifestFetchResult", region)
        self.assertIn("FetchHlsManifestForServing", region)
        self.assertIn("BuildHlsContentFeatures()", region)
        self.assertIn("<< manifest.text.size()", region)
        self.assertIn('Accept-Ranges: none', region)

    def test_posix_non_hls_accept_ranges_bytes_preserved(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn('Accept-Ranges: bytes', src)

    def test_posix_spoofSamsung_unchanged(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn("Content-Length: 1073741824", src)
        self.assertIn("Accept-Ranges: none", src)

    # --- Symmetry between both files ---

    def test_both_platforms_use_hls_fetch(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertIn("HlsManifestFetchResult", src)
            self.assertIn("FetchHlsManifestForServing", src)
            self.assertIn('L"video/mpegurl"', src)

    def test_neither_platform_has_ishlsmanifest(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertNotIn("isHlsManifest", src,
                             "isHlsManifest must not exist")

    def test_both_platforms_spoof_value_unchanged(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertIn("Content-Length: 1073741824", src)
            self.assertIn("Accept-Ranges: none", src)


# =========================================================================
# Black-box tests (use the live DLNA server binary)
# =========================================================================

import os
import socket
import subprocess
import threading
import time
import urllib.error
import urllib.request
from contextlib import contextmanager
from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path

import pytest

from tests.fixtures.soap_client import (
    build_browse_envelope,
    build_system_update_id_envelope,
    parse_browse_response,
    parse_system_update_id_response,
    build_search_envelope,
)


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _launch_dlna(binary_path, port, media_sources_str):
    binary_dir = Path(binary_path).parent
    config_ini = binary_dir / "config.ini"
    old = None
    if config_ini.exists():
        old = config_ini.read_text(encoding="utf-8-sig")
    config_ini.write_text(
        "[Settings]\n"
        f"Port={port}\n"
        f"MediaSources={media_sources_str}\n"
        f"DebugLog=1\n",
        encoding="utf-8-sig",
    )
    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
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
        except (ConnectionRefusedError, OSError, socket.timeout):
            time.sleep(0.1)
    return proc, connected, old, config_ini


def _stop_dlna(proc, old, config_ini):
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


HLS_MANIFEST_TEXT = """#EXTM3U
#EXT-X-TARGETDURATION:10
#EXT-X-VERSION:3
#EXTINF:10.0,
segment_001.ts
#EXTINF:10.0,
segment_002.ts
#EXTINF:10.0,
segment_003.ts
"""


class _HlsServerHandler(BaseHTTPRequestHandler):
    hls_text = HLS_MANIFEST_TEXT

    def do_GET(self):
        if self.path == "/playlist.m3u8":
            body = self.hls_text.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "video/mpegurl")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()

    def log_message(self, *a):
        pass


@contextmanager
def _hls_http_server():
    port = _free_port()
    server = HTTPServer(("127.0.0.1", port), _HlsServerHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield port, server
    finally:
        server.shutdown()
        thread.join(timeout=5)


def _soap_request(base_url, envelope, action):
    url = f"{base_url}/upnp/control/content_directory"
    headers = {
        "Content-Type": 'text/xml; charset="utf-8"',
        "SOAPACTION":
            f'"urn:schemas-upnp-org:service:ContentDirectory:1#{action}"',
    }
    req = urllib.request.Request(
        url, data=envelope.encode("utf-8"), headers=headers, method="POST")
    with urllib.request.urlopen(req) as resp:
        return resp.read().decode("utf-8")


def _browse_for_hls_item(base_url, max_retries=20, interval=0.5):
    """Browse root and find the first video/mpegurl item.
    Polls until found (scan runs async after Phase 1)."""
    import html as _html
    import xml.etree.ElementTree as ET
    for _ in range(max_retries):
        env = build_browse_envelope(object_id="0")
        xml = _soap_request(base_url, env, "Browse")
        parsed = parse_browse_response(xml)
        result_xml = parsed.get("Result", "")
        if result_xml:
            unescaped = _html.unescape(result_xml)
            root = ET.fromstring(unescaped)
            for item in root.iter(
                    "{urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/}item"):
                res = item.find(
                    "{urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/}res")
                upnp_class = item.find(
                    "{urn:schemas-upnp-org:metadata-1-0/upnp/}class")
                if upnp_class is not None and \
                        "videoItem" in (upnp_class.text or ""):
                    if res is not None and res.text:
                        path = res.text
                        item_id = path.rstrip("/").rsplit("/", 1)[-1]
                        return int(item_id)
                if res is not None and res.text:
                    path = res.text
                    item_id = path.rstrip("/").rsplit("/", 1)[-1]
                    return int(item_id)
        time.sleep(interval)
    return None


class TestHlsServedManifestHasAbsoluteUris:
    def test_served_manifest_has_absolute_uris(self, dlna_binary):
        with _hls_http_server() as (hls_port, server):
            url = f"http://127.0.0.1:{hls_port}/playlist.m3u8"
            dlna_port = _free_port()
            proc, ok, old, ini = _launch_dlna(
                dlna_binary, dlna_port, url)
            if not ok:
                _stop_dlna(proc, old, ini)
                pytest.fail(f"DLNA server not listening on {dlna_port}")
            try:
                base = f"http://127.0.0.1:{dlna_port}"
                item_id = _browse_for_hls_item(base)
                if item_id is None:
                    # Debug: dump browse response
                    try:
                        import html as _html
                        import xml.etree.ElementTree as ET
                        env = build_browse_envelope(object_id="0")
                        raw_xml = _soap_request(base, env, "Browse")
                        print(f"\n[debug] HLS test browse response:")
                        print(raw_xml[:2000])
                        # Also check SystemUpdateID
                        suid_env = build_system_update_id_envelope()
                        suid_raw = _soap_request(
                            base, suid_env,
                            "GetSystemUpdateID")
                        print(f"[debug] SystemUpdateID: {suid_raw[:500]}")
                    except Exception as e:
                        print(f"[debug] Error dumping: {e}")
                assert item_id is not None, (
                    "No HLS media item found via Browse")

                req = urllib.request.Request(f"{base}/media/{item_id}")
                with urllib.request.urlopen(req) as resp:
                    body = resp.read().decode("utf-8")

                for line in body.splitlines():
                    stripped = line.strip()
                    if not stripped or stripped.startswith("#"):
                        continue
                    assert stripped.startswith("http://") or \
                        stripped.startswith("https://"), (
                        f"Non-absolute URI in served manifest: {stripped}")
            finally:
                _stop_dlna(proc, old, ini)


class TestHlsFetchFailureReturns502:
    def test_hls_fetch_failure_returns_502_not_a_hang(
            self, dlna_binary):
        with _hls_http_server() as (hls_port, server):
            url = f"http://127.0.0.1:{hls_port}/playlist.m3u8"
            dlna_port = _free_port()
            proc, ok, old, ini = _launch_dlna(
                dlna_binary, dlna_port, url)
            if not ok:
                _stop_dlna(proc, old, ini)
                pytest.fail(f"DLNA server not listening on {dlna_port}")
            try:
                base = f"http://127.0.0.1:{dlna_port}"
                item_id = _browse_for_hls_item(base)
                assert item_id is not None, (
                    "No HLS item found before shutdown")
            finally:
                server.shutdown()

            try:
                req = urllib.request.Request(f"{base}/media/{item_id}")
                with urllib.request.urlopen(req) as resp:
                    resp.read()
                pytest.fail("Expected 502, got 200")
            except urllib.error.HTTPError as e:
                assert e.code == 502, (
                    f"Expected 502 Bad Gateway, got {e.code}")
            finally:
                _stop_dlna(proc, old, ini)


if __name__ == "__main__":
    unittest.main()