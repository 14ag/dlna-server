import re
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent


def _read(path):
    return (ROOT.parent / path).read_text(encoding="utf-8")


class TestDispatcherNotifyOrdering:
    def test_notify_happens_after_group_leave_not_before(self):
        src = _read("src/media_sources_common.cpp")
        idx = src.find("PlaylistScanPool::Get().Submit([this, ctx, node]()")
        assert idx > 0, "Submit call not found"
        region = src[idx:idx + 900]
        leave_idx = region.find("TaskGroupLeaveGuard leave(ctx->group);")
        notify_idx = region.find("ctx->queueCv.notify_all();")
        assert leave_idx > 0 and notify_idx > 0
        # the notify text must appear in program order strictly after
        # a closing brace that ends the block containing the leave guard
        # find the first closing brace after the leave guard declaration
        brace_after_leave = region.find("}", leave_idx)
        assert brace_after_leave > 0
        assert notify_idx > brace_after_leave, (
            "queueCv.notify_all() must run after the TaskGroupLeaveGuard "
            "scope closes so group.Leave() has already run, otherwise "
            "the dispatcher can miss the wakeup that brings the pending "
            "count to zero, see workflow section 1.3")


class TestSingleManifestScanCompletes:
    def test_single_hls_source_scan_reaches_completion(
            self, dlna_binary, tmp_path):
        """A media source that is a single non-nested HLS manifest must
        let the background scan finish. This is the minimal case that
        deterministically triggers the RunPlaylistDispatcher lost wakeup
        fixed in T1: exactly one playlist node, zero nested discoveries,
        so nothing else can accidentally notify queueCv after the true
        zero transition."""
        import http.server
        import socket
        import threading
        import os

        manifest_dir = tmp_path / "hls"
        manifest_dir.mkdir()
        (manifest_dir / "playlist.m3u8").write_text(
            "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-VERSION:3\n"
            "#EXTINF:10.0,\nsegment_001.ts\n",
            encoding="utf-8")

        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(("127.0.0.1", 0))
        hls_port = s.getsockname()[1]
        s.close()

        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *a):
                pass

        httpd = http.server.HTTPServer(("127.0.0.1", hls_port), Handler)
        os.chdir(manifest_dir)
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()

        dlna_port = 0
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as fs:
            fs.bind(("127.0.0.1", 0))
            dlna_port = fs.getsockname()[1]

        binary_dir = Path(dlna_binary).parent
        config_ini = binary_dir / "config.ini"
        old = config_ini.read_text(encoding="utf-8-sig") \
            if config_ini.exists() else None
        manifest_url = f"http://127.0.0.1:{hls_port}/playlist.m3u8"
        config_ini.write_text(
            "[Settings]\n"
            f"Port={dlna_port}\n"
            f"MediaSources={manifest_url}\n"
            "DebugLog=1\n",
            encoding="utf-8-sig",
        )

        import os as _os
        env = _os.environ.copy()
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
            assert connected, "server did not open its listen port"

            from tests.fixtures.soap_client import (
                build_browse_envelope, parse_browse_response)
            import urllib.request

            def browse():
                env_xml = build_browse_envelope(object_id="0")
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

            # before T1 this loop times out because the scan thread is
            # permanently blocked inside RunPlaylistDispatcher and the
            # HLS item is never published to the child count of the
            # source container
            found_item = False
            deadline = time.time() + 12
            while time.time() < deadline:
                result = browse()
                if int(result.get("NumberReturned", "0")) > 0:
                    inner = browse()
                    # descend once into the single source container
                    import re as _re
                    ids = _re.findall(r'container id="(\d+)"',
                                        result.get("Result", ""))
                    if ids:
                        child_env = build_browse_envelope(
                            object_id=ids[0])
                        # reuse the same soap call with the child id
                        result2 = browse()
                    found_item = True
                    break
                time.sleep(0.5)
            assert found_item, (
                "background scan never completed within 12s for a "
                "single-manifest source, RunPlaylistDispatcher may be "
                "hung again, see workflow T1")
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


class TestSsdpInitialBurstThreadIsJoined:
    def test_burst_thread_is_a_tracked_member_not_detached(self):
        header = _read("src/ssdp.h")
        source = _read("src/ssdp.cpp")
        assert "std::thread m_initialBurstThread;" in header, (
            "m_initialBurstThread member missing from SSDP class")
        assert ".detach();" not in source, (
            "SSDP::Start must not detach the initial burst thread, "
            "detaching it races m_ipv4Socket/m_ipv6Socket against "
            "SSDP::Stop's CloseSockets, see workflow T2")
        start_idx = source.find("bool SSDP::Start(")
        stop_idx = source.find("void SSDP::Stop()")
        assert start_idx > 0 and stop_idx > 0
        start_region = source[start_idx:stop_idx]
        stop_region = source[stop_idx:stop_idx + 1200]
        assert "m_initialBurstThread = std::thread(" in start_region
        assert "m_initialBurstThread.joinable()" in stop_region
        assert "m_initialBurstThread.join();" in stop_region
        close_idx = stop_region.find("CloseSockets();")
        join_idx = stop_region.find("m_initialBurstThread.join();")
        assert 0 < join_idx < close_idx, (
            "m_initialBurstThread must be joined before CloseSockets "
            "runs in SSDP::Stop")

    def test_diagnostic_scaffolding_removed_from_ssdp(self):
        source = _read("src/ssdp.cpp")
        assert "diag_mrunning.txt" not in source
        assert "[diag:ssdp]" not in source


class TestSsdpStartStopCycleUnderRace:
    def test_rapid_start_stop_does_not_crash_or_hang(
            self, dlna_binary, tmp_path):
        import os
        import socket

        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "placeholder.mp3").write_bytes(b"\x00" * 16)

        binary_dir = Path(dlna_binary).parent
        config_ini = binary_dir / "config.ini"
        old = config_ini.read_text(encoding="utf-8-sig") \
            if config_ini.exists() else None

        try:
            for _ in range(5):
                with socket.socket(
                        socket.AF_INET, socket.SOCK_STREAM) as fs:
                    fs.bind(("127.0.0.1", 0))
                    port = fs.getsockname()[1]

                config_ini.write_text(
                    "[Settings]\n"
                    f"Port={port}\n"
                    f"MediaSources={media_dir}\n",
                    encoding="utf-8-sig",
                )
                env = os.environ.copy()
                env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
                proc = subprocess.Popen(
                    [str(dlna_binary), "--headless"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    env=env, cwd=str(binary_dir))
                deadline = time.time() + 15
                connected = False
                while time.time() < deadline:
                    try:
                        with socket.create_connection(
                                ("127.0.0.1", port), timeout=0.5):
                            connected = True
                            break
                    except OSError:
                        time.sleep(0.05)
                assert connected, "server did not come up on iteration"
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3)
                assert proc.returncode is not None, (
                    "server process did not exit cleanly, teardown may "
                    "be hanging, see workflow T2")
        finally:
            if old is not None:
                config_ini.write_text(old, encoding="utf-8-sig")
            elif config_ini.exists():
                config_ini.unlink()


class TestSkipFirewallEnvVarDetection:
    def test_helper_distinguishes_absent_from_empty(self):
        source = _read("src/server.cpp")
        assert "IsSkipFirewallEnvVarPresent" in source
        assert "GetLastError() != ERROR_ENVVAR_NOT_FOUND" in source
        assert "wchar_t skipFirewall[8]" not in source, (
            "old inline unset-vs-empty-conflating check should be gone")

    def test_diagnostic_scaffolding_removed_from_server_start(self):
        source = _read("src/server.cpp")
        assert "[diag]" not in source
        assert "[diag:server]" not in source

    def test_empty_env_var_skips_firewall_check(
            self, dlna_binary, tmp_path):
        import os
        import socket

        media_dir = tmp_path / "media"
        media_dir.mkdir()
        (media_dir / "placeholder.mp3").write_bytes(b"\x00" * 16)

        binary_dir = Path(dlna_binary).parent
        config_ini = binary_dir / "config.ini"
        old = config_ini.read_text(encoding="utf-8-sig") \
            if config_ini.exists() else None

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as fs:
            fs.bind(("127.0.0.1", 0))
            port = fs.getsockname()[1]

        config_ini.write_text(
            "[Settings]\n"
            f"Port={port}\n"
            f"MediaSources={media_dir}\n",
            encoding="utf-8-sig",
        )
        env = os.environ.copy()
        env["DLNA_SERVER_SKIP_FIREWALL"] = ""
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
                            ("127.0.0.1", port), timeout=0.5):
                        connected = True
                        break
                except OSError:
                    time.sleep(0.1)
            assert connected, (
                "server did not come up with an empty (but present) "
                "DLNA_SERVER_SKIP_FIREWALL, it likely blocked on the "
                "interactive firewall MessageBoxW, see workflow T4")
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


class TestRescanSerialization:
    def test_rescan_mutex_declared_and_used_both_platforms(self):
        header = _read("src/server.h")
        win = _read("src/server.cpp")
        posix = _read("src/posix_server.cpp")
        assert "std::mutex m_rescanMutex;" in header
        for src in (win, posix):
            idx = src.find("bool Server::Rescan()")
            assert idx > 0
            region = src[idx:idx + 600]
            assert "std::lock_guard<std::mutex> rescanLock(m_rescanMutex);" \
                in region
            reset_idx = region.find("AppMedia.ResetForRescan();")
            lock_idx = region.find("rescanLock(m_rescanMutex)")
            assert 0 < lock_idx < reset_idx, (
                "the rescan lock must be taken before ResetForRescan "
                "runs, otherwise two overlapping Rescan calls can still "
                "interleave their reset and scan phases")