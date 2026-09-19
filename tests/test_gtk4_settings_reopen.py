import subprocess
import pytest


@pytest.mark.posix_only
@pytest.mark.needs_xvfb
def test_settings_dialog_reopens_after_close(gtk_binary, xvfb_env):
    result = subprocess.run(
        [str(gtk_binary), "--print-settings-reopen"],
        capture_output=True, text=True, timeout=60, env=xvfb_env,
    )
    assert result.returncode == 0, result.stderr
    out = result.stdout
    assert "first-open=1" in out
    assert "slot-cleared=1" in out
    assert "second-open=1" in out
