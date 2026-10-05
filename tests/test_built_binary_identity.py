import os
import subprocess

import pytest

APP_ID = "com.github.dlna-server-14ag"


def run_hook(binary, flag):
    result = subprocess.run([binary, flag], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0
    return result.stdout.strip()


def test_binary_reports_branded_app_id(dlna_binary):
    assert run_hook(dlna_binary, "--print-app-id") == APP_ID


def test_server_header_carries_generated_version(dlna_binary):
    expected = os.environ.get("DLNA_EXPECTED_VERSION")
    if not expected:
        pytest.skip("DLNA_EXPECTED_VERSION not set")
    header = run_hook(dlna_binary, "--print-dlna-server-header")
    assert header.endswith("dlna-server/" + expected)
