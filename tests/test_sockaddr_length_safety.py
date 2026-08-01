import subprocess

import pytest


@pytest.mark.windows_only
def test_sockaddr_length_within_capacity_is_safe(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-sockaddr-length-safety", "28", "128"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "1"


@pytest.mark.windows_only
def test_sockaddr_length_exceeding_capacity_is_rejected(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-sockaddr-length-safety", "4096", "128"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "0"


@pytest.mark.windows_only
def test_sockaddr_length_zero_is_rejected(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-sockaddr-length-safety", "0", "128"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "0"


@pytest.mark.windows_only
def test_sockaddr_length_negative_is_rejected(dlna_binary):
    result = subprocess.run(
        [dlna_binary, "--print-sockaddr-length-safety", "-1", "128"],
        capture_output=True, text=True, timeout=10,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "0"
