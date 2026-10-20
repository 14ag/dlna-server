import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only

CLI_CANDIDATES = ["./dlna-server", "/usr/bin/dlna-server", "/usr/local/bin/dlna-server"]
GUI_CANDIDATES = ["./dlna-server-gui", "/usr/bin/dlna-server-gui", "/usr/local/bin/dlna-server-gui"]
DEADLOCK_TEXT = "Resource deadlock avoided"


def _first_existing(paths):
    for candidate in paths:
        if os.path.exists(candidate):
            return candidate
    return None


def _isolated_env():
    env = os.environ.copy()
    config_root = Path(tempfile.mkdtemp(prefix="dlna-config-"))
    runtime_root = Path(tempfile.mkdtemp(prefix="dlna-runtime-"))
    env["HOME"] = str(config_root)
    env["XDG_CONFIG_HOME"] = str(config_root)
    env["XDG_RUNTIME_DIR"] = str(runtime_root)
    return env


def test_cli_help_does_not_deadlock_on_fresh_install():
    binary_path = _first_existing(CLI_CANDIDATES)
    assert binary_path is not None
    result = subprocess.run(
        [binary_path, "--help"],
        capture_output=True,
        text=True,
        timeout=10,
        env=_isolated_env(),
    )
    assert DEADLOCK_TEXT not in result.stderr
    assert result.returncode == 0


def test_cli_help_repeated_runs_stay_stable():
    binary_path = _first_existing(CLI_CANDIDATES)
    assert binary_path is not None
    for _ in range(1, 6):
        result = subprocess.run(
            [binary_path, "--help"],
            capture_output=True,
            text=True,
            timeout=10,
            env=_isolated_env(),
        )
        assert DEADLOCK_TEXT not in result.stderr
        assert result.returncode == 0


def test_gui_binary_does_not_deadlock_on_fresh_install():
    binary_path = _first_existing(GUI_CANDIDATES)
    if binary_path is None:
        pytest.fail("gui binary not built or not installed on this machine")

    process = subprocess.Popen(
        [binary_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=_isolated_env(),
    )
    try:
        stdout_text, stderr_text = process.communicate(timeout=3)
        assert DEADLOCK_TEXT not in stderr_text
    except subprocess.TimeoutExpired:
        process.terminate()
        try:
            stdout_text, stderr_text = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout_text, stderr_text = process.communicate()
        assert DEADLOCK_TEXT not in stderr_text
