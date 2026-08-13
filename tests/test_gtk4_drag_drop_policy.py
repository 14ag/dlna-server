import subprocess
import pytest

@pytest.mark.posix_only
def test_should_allow_source_drop_matches_windows_policy(dlna_binary):
    # exercises the shared pure predicate the new gtk4 drop target calls
    # into it is already platform neutral this only re confirms it after
    # this task adds a second call site to it
    allowed = subprocess.run(
         [dlna_binary, "--print-should-allow-source-drop", "0"],
         capture_output=True, text=True, timeout=5,
     )
    disallowed = subprocess.run(
        [dlna_binary, "--print-should-allow-source-drop", "1"],
        capture_output=True, text=True, timeout=5,
    )
    assert allowed.stdout.strip() == "1"
    assert disallowed.stdout.strip() == "0"