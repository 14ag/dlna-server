import os
import subprocess


def test_second_add_is_rejected(dlna_binary, tmp_path):
    env = dict(os.environ, HOME=str(tmp_path), XDG_CONFIG_HOME=str(tmp_path))
    result = subprocess.run(
        [dlna_binary, "--print-add-media-source-if-new"],
        capture_output=True,
        text=True,
        env=env,
        timeout=30,
    )
    assert result.returncode == 0
    lines = result.stdout.split()
    assert "first=1" in lines
    assert "second=0" in lines
    assert "matches=1" in lines
