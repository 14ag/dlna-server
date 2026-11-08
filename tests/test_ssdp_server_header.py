import re
import subprocess


def test_server_header_format(dlna_binary):
    """UDA SERVER format with DLNADOC before UPnP/1.0 (ReadyMedia ordering); UPnP/1.1 is a spec deviation.
    Linux/macOS builds emit a bare platform token (DLNA_PLATFORM_NAME), Windows emits Windows/10.0."""
    result = subprocess.run(
        [str(dlna_binary), "--print-dlna-server-header"],
        capture_output=True, text=True, timeout=30)
    assert result.returncode == 0
    assert re.fullmatch(
        r"\S+ DLNADOC/1\.50 UPnP/1\.0 dlna-server/\d+\.\d+\.\d+",
        result.stdout.strip())
