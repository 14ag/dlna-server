import os
import subprocess
import tempfile


def _run_truncation_hook(dlna_binary, log_path):
    result = subprocess.run(
        [dlna_binary, "--print-debug-log-session-truncation", log_path],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0
    return result.stdout


def test_debug_log_clears_leftover_content_from_previous_session(dlna_binary):
    with tempfile.TemporaryDirectory() as tmp_dir:
        log_path = os.path.join(tmp_dir, "debug.log")
        stdout = _run_truncation_hook(dlna_binary, log_path)
        with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
            content = handle.read()
        assert "leftover line from a previous session" not in content
        assert "session line one" in content
        assert "session line two" in content
        assert "same-handle-reused=1" in stdout


def test_debug_log_appends_within_the_same_session_in_order(dlna_binary):
    with tempfile.TemporaryDirectory() as tmp_dir:
        log_path = os.path.join(tmp_dir, "debug.log")
        _run_truncation_hook(dlna_binary, log_path)
        with open(log_path, "r", encoding="utf-8", errors="replace") as handle:
            content = handle.read()
        assert content.index("session line one") < content.index("session line two")
