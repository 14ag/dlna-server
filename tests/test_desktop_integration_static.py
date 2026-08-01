import shutil
import subprocess
import os
from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only

REPO_ROOT = Path(__file__).resolve().parent.parent
PACKAGING_DIR = REPO_ROOT / "packaging" / "linux"


def _find_desktop_template():
    candidates = list(PACKAGING_DIR.glob("*.desktop.in"))
    if not candidates:
        candidates = list(PACKAGING_DIR.glob("*.desktop"))
    return candidates[0] if candidates else None


DESKTOP_TEMPLATE = _find_desktop_template()
BUILT_DESKTOP_FILE = (
    REPO_ROOT / "build-release-linux-stage" / "install" / "share" / "applications" / "dlna-server.desktop"
)


def _sudo_run(*args):
    if os.name == "nt":
        raise RuntimeError("sudo not available on Windows")
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        return subprocess.run(args, check=True, capture_output=True, text=True)
    if shutil.which("sudo") is None:
        raise RuntimeError("sudo not available")
    password = os.environ.get("DLNA_SUDO_PASSWORD")
    if password is not None:
        return subprocess.run(
            ["sudo", "-S", "-p", "", *args],
            input=f"{password}\n",
            check=True,
            capture_output=True,
            text=True,
        )
    return subprocess.run(["sudo", "-n", *args], check=True, capture_output=True, text=True)


def _ensure_desktop_file_validate():
    validator = shutil.which("desktop-file-validate")
    if validator is not None:
        return validator
    if os.name == "nt":
        return None
    if shutil.which("apt-get") is None or shutil.which("sudo") is None:
        return None
    try:
        _sudo_run("apt-get", "update", "-qq")
        _sudo_run("apt-get", "install", "-y", "-qq", "desktop-file-utils")
    except (OSError, subprocess.CalledProcessError):
        # Non-interactive sudo unavailable (e.g. passwordless apt on CI
        # hosts only): leave the validator uninstalled and skip the test.
        return None
    return shutil.which("desktop-file-validate")


@pytest.mark.skipif(
    DESKTOP_TEMPLATE is None,
    reason="no *.desktop(.in) file found under packaging/linux verify task four checklist manually",
)
def test_desktop_file_icon_matches_installed_icon_name():
    text = DESKTOP_TEMPLATE.read_text()
    assert "Icon=dlna-server" in text, (
        "Icon= must be the bare name dlna-server matching the RENAME "
        "dlna-server.png install rules in CMakeLists.txt"
    )
    assert ".png" not in text.split("Icon=", 1)[1].splitlines()[0], (
        "Icon= must not include a file extension"
    )


@pytest.mark.skipif(
    DESKTOP_TEMPLATE is None,
    reason="no *.desktop(.in) file found under packaging/linux verify task four checklist manually",
)
def test_desktop_file_startup_wm_class_matches_fltk_xclass():
    text = DESKTOP_TEMPLATE.read_text()
    fltk_main = (REPO_ROOT / "src" / "fltk_gui_main.cpp").read_text()
    assert 'xclass("dlna-server")' in fltk_main, (
        "fltk_gui_main.cpp xclass value changed update this test and the "
        "desktop file StartupWMClass together"
    )
    assert "StartupWMClass=dlna-server" in text


def test_desktop_file_validate_reports_no_errors():
    validator = _ensure_desktop_file_validate()
    if validator is None:
        pytest.skip("desktop-file-validate unavailable and sudo not usable")
    if not BUILT_DESKTOP_FILE.is_file():
        pytest.fail(
            f"built desktop file missing: {BUILT_DESKTOP_FILE} run Linux build before validation"
        )
    result = subprocess.run(
        [validator, str(BUILT_DESKTOP_FILE)], capture_output=True, text=True
    )
    assert result.returncode == 0, result.stdout + result.stderr
