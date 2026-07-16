import subprocess
import pytest


@pytest.mark.parametrize("csv,expected", [
    ("0,0", "0"),
    ("0,3", "1"),
    ("5,0", "1"),
    ("", "0"),
    ("0,0,0,1", "1"),
])
def test_any_field_has_content(dlna_binary, csv, expected):
    result = subprocess.run(
        [dlna_binary, "--print-any-field-has-content", csv],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0
    assert result.stdout.strip() == expected
