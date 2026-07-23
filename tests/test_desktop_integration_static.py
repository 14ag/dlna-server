import subprocess
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
    import shutil

    validator = shutil.which("desktop-file-validate")
    if validator is None or DESKTOP_TEMPLATE is None:
        pytest.skip("desktop-file-validate or the desktop file is unavailable")
    if DESKTOP_TEMPLATE.suffix == ".in":
        pytest.skip("template file has unexpanded cmake variables validate the built one instead")
    result = subprocess.run(
        [validator, str(DESKTOP_TEMPLATE)], capture_output=True, text=True
    )
    assert result.returncode == 0, result.stdout + result.stderr
