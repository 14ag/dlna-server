import subprocess
import pytest


@pytest.mark.parametrize("cb_data,expected", [
    ("0", "0"),           # zero size is never plausible
    ("1", "0"),           # 1 byte is not a whole multiple of sizeof(wchar_t)
    ("2", "1"),            # exactly one wchar_t unit (sizeof(wchar_t) assumed 2 on this build; adjust if this project targets a 4-byte wchar_t platform)
    ("3", "0"),           # not a whole multiple
    ("4", "1"),           # two wchar_t units
    ("4294967295", "0"),  # ULONG_MAX: not a multiple of 2
    ("4294967294", "1"),  # ULONG_MAX - 1: even, plausible by this predicate alone
])
def test_is_plausible_copydata_size(dlna_binary, cb_data, expected):
    result = subprocess.run(
        [dlna_binary, "--print-is-plausible-copydata-size", cb_data],
        capture_output=True, text=True, timeout=10,
    )
    assert result.stdout.strip() == expected
