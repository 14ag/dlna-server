import re
import subprocess

import pytest


def _run(binary_path, *args, timeout=30):
    return subprocess.run(
        [str(binary_path), *args],
        capture_output=True, text=True, timeout=timeout,
    )


def test_server_header_uses_literal_upnp_1_0_token(dlna_binary):
    """
    UPnP Device Architecture mandates the literal SERVER header format
    'OS/version UPnP/1.0 product/version'. The middle token is fixed
    protocol text, independent of which UDA minor-version features the
    device implements elsewhere (e.g. BOOTID.UPNP.ORG/CONFIGID.UPNP.ORG).
    """
    result = _run(dlna_binary, "--print-dlna-server-header")
    assert result.returncode == 0
    header = result.stdout.strip()
    assert " UPnP/1.0 " in header
    assert "UPnP/1.1" not in header


def test_server_header_includes_dlnadoc_token(dlna_binary):
    """
    DLNA-class control points (smart TVs, consoles, AV receivers) use the
    DLNADOC token in the SSDP SERVER header to fast-path trust a device as
    a DLNA media server. Format and ordering match ReadyMedia/MiniDLNA's
    field-proven convention: 'OS/version DLNADOC/1.50 UPnP/1.0 product/version'.
    """
    result = _run(dlna_binary, "--print-dlna-server-header")
    assert result.returncode == 0
    header = result.stdout.strip()
    assert "DLNADOC/1.50" in header
    # DLNADOC must precede UPnP/1.0, matching MiniDLNA's proven ordering.
    dlnadoc_pos = header.index("DLNADOC/1.50")
    upnp_pos = header.index("UPnP/1.0")
    assert dlnadoc_pos < upnp_pos


def test_server_header_ends_with_product_and_version(dlna_binary):
    result = _run(dlna_binary, "--print-dlna-server-header")
    assert result.returncode == 0
    header = result.stdout.strip()
    assert re.search(r"dlna-server/\d+\.\d+\.\d+$", header)
