import os
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "version.py"


def _script_major():
    # No hardcoded major anywhere in this file: fixtures and expectations
    # follow whatever the repo script currently stamps.
    spec = importlib.util.spec_from_file_location("dlna_repo_version", str(SCRIPT))
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.MAJOR


MAJOR = _script_major()


def run(*args, env_extra=None, drop_patch_env=True, script=SCRIPT):
    env = os.environ.copy()
    if drop_patch_env:
        env.pop("DLNA_PATCH", None)
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [sys.executable, str(script), *args],
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        env=env,
    )


def isolated_script(tmp_path):
    scripts_dir = tmp_path / "scripts"
    scripts_dir.mkdir()
    script = scripts_dir / "version.py"
    shutil.copy2(SCRIPT, script)
    return script


def test_october_example(tmp_path):
    result = run("--patch", "3", "--now", "2026-10-05T14:07", script=isolated_script(tmp_path))
    assert result.returncode == 0
    assert result.stdout == f"v{MAJOR}.26.76153-build7\n"


def test_first_of_january_pads_to_two_digits(tmp_path):
    result = run("--patch", "0", "--now", "2026-01-01T00:00", script=isolated_script(tmp_path))
    assert result.stdout == f"v{MAJOR}.26.07030-build0\n"


def test_last_day_of_year_stays_five_digits(tmp_path):
    result = run("--patch", "9", "--now", "2026-12-31T23:59", script=isolated_script(tmp_path))
    assert result.stdout == f"v{MAJOR}.26.92969-build59\n"


def test_default_version_has_no_patch_suffix_when_unused(tmp_path):
    result = run("--now", "2026-10-05T14:07", script=isolated_script(tmp_path))
    assert result.stdout == f"v{MAJOR}.26.7615-build7\n"
    assert result.stderr == ""


def test_patch_increments_until_version_is_unused(tmp_path):
    script = isolated_script(tmp_path)
    output = tmp_path / "output" / "linux"
    output.mkdir(parents=True)
    (output / f"release-{MAJOR}.26.7615.deb").touch()
    (output / f"release-{MAJOR}.26.76151.flatpak").touch()

    result = run("--now", "2026-10-05T14:07", script=script)
    assert result.returncode == 0
    assert result.stdout == f"v{MAJOR}.26.76152-build7\n"


def test_patch_increments_past_single_digits(tmp_path):
    script = isolated_script(tmp_path)
    output = tmp_path / "output" / "winx64"
    output.mkdir(parents=True)
    for patch in range(10):
        suffix = "" if patch == 0 else str(patch)
        (output / f"release-{MAJOR}.26.7615{suffix}.zip").touch()

    result = run("--now", "2026-10-05T14:07", script=script)
    assert result.returncode == 0
    assert result.stdout == f"v{MAJOR}.26.761510-build7\n"


def test_patch_from_environment(tmp_path):
    result = run("--now", "2026-10-05T14:07", env_extra={"DLNA_PATCH": "5"}, script=isolated_script(tmp_path))
    assert result.stdout == f"v{MAJOR}.26.76155-build7\n"


def test_prompt_never_reaches_stdout():
    result = run("--patch", "1", "--now", "2026-10-05T14:07")
    assert result.stdout.count("\n") == 1


def test_invalid_patch_is_rejected():
    result = run("--patch", "12")
    assert result.returncode == 2
    assert result.stdout == ""


def test_numeric_of_strips_prefix_and_build():
    result = run("--numeric-of", f"v{MAJOR}.26.76153-build7")
    assert result.returncode == 0
    assert result.stdout == f"{MAJOR}.26.76153\n"


def test_numeric_of_accepts_plain_numeric():
    result = run("--numeric-of", "1.26.76153")
    assert result.stdout == "1.26.76153\n"


def test_numeric_of_rejects_garbage():
    result = run("--numeric-of", "latest")
    assert result.returncode == 2
    assert result.stdout == ""
