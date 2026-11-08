import subprocess
from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only

RESCAN_TEST_MEDIA = Path(__file__).resolve().parent / "test media" / "concurrent-test"


@pytest.fixture
def media_folder():
    assert RESCAN_TEST_MEDIA.is_dir(), (
        f"test media folder not found: {RESCAN_TEST_MEDIA}"
    )
    return str(RESCAN_TEST_MEDIA)


def test_rescan_before_start_does_not_crash(dlna_binary, media_folder):
    """Rescan before Start must not terminate: joinable-thread overwrite
    on the directory watcher calls std::terminate."""
    result = subprocess.run(
        [dlna_binary, "--source", media_folder,
         "--print-rescan-then-start-safety"],
        capture_output=True, text=True, timeout=60,
    )
    assert result.returncode == 0, (
        f"process crashed or exited abnormally: returncode={result.returncode:#x}\n"
        f"stderr={result.stderr}"
    )
    assert "start-ok=1" in result.stdout, result.stdout
    assert "done" in result.stdout, result.stdout
