import os
import shutil
import subprocess

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


def _config_path_for(binary_path):
    return os.path.join(os.path.dirname(os.path.abspath(binary_path)), "config.ini")


def _with_no_config_file(binary_path, body):
    config_path = _config_path_for(binary_path)
    backup_path = config_path + ".workflow-test-backup"
    had_existing = os.path.exists(config_path)
    if had_existing:
        shutil.move(config_path, backup_path)
    try:
        if os.path.exists(config_path):
            os.remove(config_path)
        body(config_path)
    finally:
        if os.path.exists(config_path):
            os.remove(config_path)
        if had_existing:
            shutil.move(backup_path, config_path)


def test_cli_help_does_not_deadlock_on_fresh_install():
    binary_path = _first_existing(CLI_CANDIDATES)
    assert binary_path is not None

    def body(config_path):
        result = subprocess.run([binary_path, "--help"], capture_output=True, text=True, timeout=10)
        assert DEADLOCK_TEXT not in result.stderr
        assert result.returncode == 0

    _with_no_config_file(binary_path, body)


def test_cli_help_repeated_runs_stay_stable():
    binary_path = _first_existing(CLI_CANDIDATES)
    assert binary_path is not None
    for _ in range(1, 6):
        result = subprocess.run([binary_path, "--help"], capture_output=True, text=True, timeout=10)
        assert DEADLOCK_TEXT not in result.stderr
        assert result.returncode == 0


def test_gui_binary_does_not_deadlock_on_fresh_install():
    binary_path = _first_existing(GUI_CANDIDATES)
    if binary_path is None:
        pytest.skip("gui binary not built or not installed on this machine")

    def body(config_path):
        process = subprocess.Popen(
            [binary_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
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

    _with_no_config_file(binary_path, body)
