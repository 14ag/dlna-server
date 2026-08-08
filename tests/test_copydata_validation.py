import subprocess
import sys

import ctypes
import pytest

# F-CMR-01 predicate: a WM_COPYDATA cbData is plausible iff it is a positive
# whole multiple of sizeof(wchar_t) that does not overflow when divided.
# sizeof(wchar_t) differs by platform (2 on Win32, 4 on POSIX). Compute the
# expectation from the running interpreter's wchar_t size so the test is
# correct on every platform instead of hardcoding one size.
WCHAR_SIZE = ctypes.sizeof(ctypes.c_wchar)


def _expected(cb_data):
    n = int(cb_data)
    return "1" if (n > 0 and n % WCHAR_SIZE == 0) else "0"


@pytest.mark.parametrize("cb_data", [
    "0",              # zero size is never plausible
    "1",              # smaller than one wchar_t unit is never plausible
    str(WCHAR_SIZE),  # exactly one wchar_t unit
    str(WCHAR_SIZE - 1),
    str(WCHAR_SIZE * 2),  # two wchar_t units
    "4294967295",    # ULONG_MAX: not a whole multiple
    "4294967294",    # ULONG_MAX - 1: plausible iff even (Win) / divisible by 4 (POSIX)
])
def test_is_plausible_copydata_size(dlna_binary, cb_data):
    expected = _expected(cb_data)
    result = subprocess.run(
        [dlna_binary, "--print-is-plausible-copydata-size", cb_data],
        capture_output=True, text=True, timeout=10,
    )
    assert result.stdout.strip() == expected
