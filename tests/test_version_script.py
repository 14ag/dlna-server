import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "version.py"


def run(*args, env_extra=None, drop_patch_env=True):
    env = os.environ.copy()
    if drop_patch_env:
        env.pop("DLNA_PATCH", None)
    if env_extra:
        env.update(env_extra)
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        env=env,
    )


def test_october_example():
    result = run("--patch", "3", "--now", "2026-10-05T14:07")
    assert result.returncode == 0
    assert result.stdout == "v1.26.76153-build7\n"


def test_first_of_january_pads_to_two_digits():
    result = run("--patch", "0", "--now", "2026-01-01T00:00")
    assert result.stdout == "v1.26.07030-build0\n"


def test_last_day_of_year_stays_five_digits():
    result = run("--patch", "9", "--now", "2026-12-31T23:59")
    assert result.stdout == "v1.26.92969-build59\n"


def test_default_patch_without_terminal_is_zero():
    result = run("--now", "2026-10-05T14:07")
    assert result.stdout == "v1.26.76150-build7\n"


def test_patch_from_environment():
    result = run("--now", "2026-10-05T14:07", env_extra={"DLNA_PATCH": "5"})
    assert result.stdout == "v1.26.76155-build7\n"


def test_prompt_never_reaches_stdout():
    result = run("--patch", "1", "--now", "2026-10-05T14:07")
    assert result.stdout.count("\n") == 1


def test_invalid_patch_is_rejected():
    result = run("--patch", "12")
    assert result.returncode == 2
    assert result.stdout == ""


def test_numeric_of_strips_prefix_and_build():
    result = run("--numeric-of", "v1.26.76153-build7")
    assert result.returncode == 0
    assert result.stdout == "1.26.76153\n"


def test_numeric_of_accepts_plain_numeric():
    result = run("--numeric-of", "1.26.76153")
    assert result.stdout == "1.26.76153\n"


def test_numeric_of_rejects_garbage():
    result = run("--numeric-of", "latest")
    assert result.returncode == 2
    assert result.stdout == ""
