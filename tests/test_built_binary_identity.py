import os
import re
import subprocess
from pathlib import Path

import pytest

APP_ID = "com.github.dlna-server-14ag"


def run_hook(binary, flag):
    result = subprocess.run([binary, flag], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0
    return result.stdout.strip()


def _generated_version(dlna_binary):
    """Version baked into the binary at build time, from the generated
    version.h next to the build tree. Lets the test run without the
    DLNA_EXPECTED_VERSION env var after a plain local build.
    Windows-only: POSIX builds happen in a temp dir, so no in-tree
    header exists there (any build-*/ tree present is a stale Windows
    one and must not be used)."""
    root = Path(__file__).resolve().parents[1]
    name = Path(dlna_binary).parent.name
    mapping = {"winx64": "build-release-winx64", "winx86": "build-release-winx86"}
    if name not in mapping:
        return None
    header = root / mapping[name] / "generated" / "version.h"
    if not header.exists():
        return None
    match = re.search(
        r'#define\s+DLNA_SERVER_VERSION_STRING\s+"([^"]+)"',
        header.read_text(encoding="utf-8", errors="replace"),
    )
    if match:
        return match.group(1)
    return None


def test_binary_reports_branded_app_id(dlna_binary):
    assert run_hook(dlna_binary, "--print-app-id") == APP_ID


def test_server_header_carries_generated_version(dlna_binary):
    expected = os.environ.get("DLNA_EXPECTED_VERSION") or _generated_version(dlna_binary)
    header = run_hook(dlna_binary, "--print-dlna-server-header")
    if expected is None:
        # No pinned version and no build tree to read it from (e.g. a
        # packaged install): still verify the header carries a version.
        assert re.search(r"dlna-server/\d+\.\d+\.\d+$", header), header
    else:
        # The leading major is deliberately not pinned: a major bump must
        # never fail this test on its own. Compare everything after it.
        want = expected.split(".", 1)[1]
        match = re.search(r"dlna-server/\d+\.(.+)$", header)
        assert match is not None and match.group(1) == want, (header, expected)
