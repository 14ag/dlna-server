import subprocess

import pytest


def _chunk_plan(dlna_binary, total_bytes):
    result = subprocess.run(
        [dlna_binary, "--print-transmitfile-chunk-plan", str(total_bytes)],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0
    return [int(line) for line in result.stdout.splitlines() if line.strip()]


@pytest.mark.windows_only
def test_small_file_is_a_single_chunk(dlna_binary):
    assert _chunk_plan(dlna_binary, 1000) == [1000]


@pytest.mark.windows_only
def test_exact_boundary_is_a_single_chunk(dlna_binary):
    assert _chunk_plan(dlna_binary, 2147483646) == [2147483646]


@pytest.mark.windows_only
def test_one_byte_over_boundary_splits_into_two_chunks(dlna_binary):
    assert _chunk_plan(dlna_binary, 2147483647) == [2147483646, 1]


@pytest.mark.windows_only
def test_large_multi_gigabyte_file_splits_correctly(dlna_binary):
    total = 2147483646 * 2 + 500
    chunks = _chunk_plan(dlna_binary, total)
    assert chunks == [2147483646, 2147483646, 500]
    assert sum(chunks) == total
    assert all(chunk <= 2147483646 for chunk in chunks)


@pytest.mark.windows_only
def test_zero_byte_file_produces_no_chunks(dlna_binary):
    assert _chunk_plan(dlna_binary, 0) == []
