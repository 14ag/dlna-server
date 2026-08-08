import re
import subprocess

import pytest

pytestmark = pytest.mark.posix_only

# generous upper bound the pre fix synchronous path was roughly
# zero to one hundred ms jitter plus two hundred ms of sleeps between
# notify rounds plus send time so it regularly exceeded two hundred ms
# this threshold only needs to catch a regression back to that path
ELAPSED_MS_CEILING = 200


def test_start_does_not_block_on_ssdp_burst(dlna_binary, media_source_dir):
    (media_source_dir / "a.mp3").write_bytes(b"\x00" * 1024)

    result = subprocess.run(
        [str(dlna_binary), "--source", str(media_source_dir),
         "--print-ssdp-start-latency"],
        capture_output=True, text=True, timeout=30,
    )
    assert "start-ok=1" in result.stdout, result.stdout + result.stderr

    match = re.search(r"elapsed-ms=(\d+)", result.stdout)
    assert match, "elapsed-ms line missing from output: " + result.stdout
    elapsed_ms = int(match.group(1))
    assert elapsed_ms < ELAPSED_MS_CEILING, (
        f"Server.Start() took {elapsed_ms}ms; SSDP initial burst is "
        f"likely running synchronously again, see posix_ssdp.cpp SSDP::Start"
    )