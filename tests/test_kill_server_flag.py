import subprocess

import pytest


@pytest.mark.posix_only
def test_kill_with_no_running_instance_is_silent_on_stdout(dlna_binary, tmp_path):
    env = {
        "XDG_CONFIG_HOME": str(tmp_path / "config"),
        "HOME": str(tmp_path / "config"),
        "XDG_RUNTIME_DIR": str(tmp_path / "runtime"),
        "PATH": "/usr/bin:/bin",
    }
    (tmp_path / "config").mkdir()
    (tmp_path / "runtime").mkdir()

    for flag in ("--kill", "--kill-server", "-k"):
        proc = subprocess.run(
            [dlna_binary, flag], env=env, capture_output=True, text=True, timeout=15,
        )
        assert proc.stdout == "", f"{flag} printed to stdout: {proc.stdout!r}"
        assert "server is up" not in proc.stdout
        assert "server is up" not in proc.stderr
        assert proc.returncode == 1
        assert "No running dlna-server instance found." in proc.stderr


@pytest.mark.posix_only
def test_kill_stops_a_running_instance_silently(running_server, dlna_binary):
    proc = subprocess.run(
        [dlna_binary, "--kill"], capture_output=True, text=True, timeout=15,
    )
    assert proc.stdout == ""
    assert "server is up" not in proc.stdout
    assert "server is up" not in proc.stderr
    assert proc.returncode == 0
