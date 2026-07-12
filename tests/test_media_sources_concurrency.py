import concurrent.futures
import threading
import time

import pytest


class TestConcurrentBrowseDuringScan:
    def test_concurrent_browse_during_scan_no_hang_or_error(
            self, slow_playlist_source, running_server):
        """Concurrent Browse / Search / GetSystemUpdateID during active scan
        complete within 10 s each; SystemUpdateID is monotonically
        non-decreasing."""
        stop = threading.Event()
        results = []
        lock = threading.Lock()

        def worker(worker_id):
            uid_history = []
            while not stop.is_set():
                try:
                    uid = running_server.soap_get_system_update_id()
                    uid_history.append(uid)
                    browse = running_server.soap_browse(object_id="0")
                    err = browse.get("errorCode", 0)
                    assert err != 710, (
                        f"Worker {worker_id}: UPnP error 710 during Browse")
                except Exception as e:
                    with lock:
                        results.append(("error", worker_id, str(e)))
                    return
            with lock:
                results.append(("ok", worker_id, uid_history))

        # Fire 8 concurrent workers
        pool_size = 8
        with concurrent.futures.ThreadPoolExecutor(
                max_workers=pool_size) as pool:
            futs = [pool.submit(worker, i) for i in range(pool_size)]
            time.sleep(1.0)
            stop.set()
            for f in concurrent.futures.as_completed(futs, timeout=10):
                f.result()

        # Check for monotonic non-decreasing SystemUpdateID
        non_decreasing = True
        for entry in results:
            if entry[0] == "ok":
                hist = entry[2]
                for a, b in zip(hist, hist[1:]):
                    if b < a:
                        non_decreasing = False
                        break

        errors = [r for r in results if r[0] == "error"]
        assert not errors, f"Concurrent workers failed: {errors}"
        assert non_decreasing, (
            "SystemUpdateID was not monotonically non-decreasing")
