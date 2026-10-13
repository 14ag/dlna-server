import subprocess

import pytest


@pytest.mark.parametrize("text,expected", [
    ("NOTIFY * HTTP/1.1", "1"),
    ("M-SEARCH * HTTP/1.1", "0"),
    ("HTTP/1.1 200 OK", "0"),
    ("NOTIFY", "0"),
    ("notify * HTTP/1.1", "0"),
])
def test_is_ssdp_notify_start_line(dlna_binary, text, expected):
    out = subprocess.run(
        [dlna_binary, "--print-is-ssdp-notify-start-line", text],
        capture_output=True, text=True, timeout=15)
    assert out.stdout.strip() == expected
