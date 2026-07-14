import http.server
import socket
import subprocess
import threading
import time
from pathlib import Path

import pytest


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class TestPlaylistDotDotEntryResolvesCorrectly:
    def test_dotdot_entry_resolves_without_literal_dotdot_segment(
            self, dlna_binary, tmp_path):
        import os

        origin_root = tmp_path / "origin"
        (origin_root / "nested").mkdir(parents=True)
        (origin_root / "sibling.mp3").write_bytes(b"\x00" * 32)
        (origin_root / "nested" / "playlist.m3u8").write_text(
            "#EXTM3U\n#EXTINF:-1,Sibling\n../sibling.mp3\n",
            encoding="utf-8")

        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *a):
                pass

        os.chdir(origin_root)
        origin_port = _free_port()
        httpd = http.server.HTTPServer(
            ("127.0.0.1", origin_port), Handler)
        thread = threading.Thread(
            target=httpd.serve_forever, daemon=True)
        thread.start()

        playlist_url = (
            f"http://127.0.0.1:{origin_port}/nested/playlist.m3u8")
        dlna_port = _free_port()
        binary_dir = Path(dlna_binary).parent
        config_ini = binary_dir / "config.ini"
        old = config_ini.read_text(encoding="utf-8-sig") \
            if config_ini.exists() else None
        config_ini.write_text(
            "[Settings]\n"
            f"Port={dlna_port}\n"
            f"MediaSources={playlist_url}\n",
            encoding="utf-8-sig",
        )
        env = os.environ.copy()
        env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
        proc = subprocess.Popen(
            [str(dlna_binary), "--headless"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env, cwd=str(binary_dir))
        try:
            deadline = time.time() + 15
            connected = False
            while time.time() < deadline:
                try:
                    with socket.create_connection(
                            ("127.0.0.1", dlna_port), timeout=0.5):
                        connected = True
                        break
                except OSError:
                    time.sleep(0.1)
            assert connected

            import urllib.request

            def browse(object_id="0"):
                from tests.fixtures.soap_client import (
                    build_browse_envelope, parse_browse_response)
                env_xml = build_browse_envelope(object_id=object_id)
                req = urllib.request.Request(
                    f"http://127.0.0.1:{dlna_port}"
                    "/upnp/control/content_directory",
                    data=env_xml.encode("utf-8"),
                    headers={
                        "Content-Type": 'text/xml; charset="utf-8"',
                        "SOAPACTION":
                            '"urn:schemas-upnp-org:service:'
                            'ContentDirectory:1#Browse"',
                    },
                    method="POST",
                )
                with urllib.request.urlopen(req) as resp:
                    return parse_browse_response(
                        resp.read().decode("utf-8"))

            import re as _re
            resolved_url = None
            deadline = time.time() + 25
            while time.time() < deadline:
                top = browse("0")
                ids = _re.findall(
                    r'container id="(\d+)"', top.get("Result", ""))
                if not ids:
                    time.sleep(0.5)
                    continue
                result = browse(ids[0])
                result_xml = result.get("Result", "")
                for depth in range(3):
                    urls = _re.findall(
                        r"<res[^>]*>([^<]+)</res>", result_xml)
                    if urls:
                        resolved_url = urls[0]
                        break
                    inner_ids = _re.findall(
                        r'container id="(\d+)"', result_xml)
                    if not inner_ids:
                        break
                    result = browse(inner_ids[0])
                    result_xml = result.get("Result", "")
                if resolved_url:
                    break
                time.sleep(0.5)

            assert resolved_url is not None, (
                "playlist entry with a .. segment was never published, "
                "background scan may not have completed")
            assert "/../" not in resolved_url, (
                f"resolved URL still contains a literal /../ segment: "
                f"{resolved_url}, JoinUrl does not collapse dot "
                f"segments, this is the exact defect the TODO in "
                f"src/network_sources.cpp describes")
            assert resolved_url.endswith("/sibling.mp3"), (
                f"expected the .. entry to resolve up one directory to "
                f"sibling.mp3, got {resolved_url}")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
            httpd.shutdown()
            thread.join(timeout=5)
            if old is not None:
                config_ini.write_text(old, encoding="utf-8-sig")
            elif config_ini.exists():
                config_ini.unlink()
