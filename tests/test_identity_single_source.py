import subprocess
from pathlib import Path

import pytest

from tests.identity import APP_ID, GTK_APP_ID, PRODUCT_NAME

ROOT = Path(__file__).resolve().parents[1]
SCAN_ROOTS = ("src", "scripts", "packaging", "resources/gtk", "CMakeLists.txt", "build.sh", "build.bat")
LEGACY_CLEANUP = {"scripts/install_linux.sh"}
ROLE_TEMPLATES = (
    "packaging/linux/metainfo.xml.in",
    "packaging/linux/portable.desktop.in",
    "packaging/flatpak/manifest.yml.in",
)


def _read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8-sig")


def _scan_files():
    for entry in SCAN_ROOTS:
        path = ROOT / entry
        if path.is_file():
            yield path
        elif path.is_dir():
            yield from (p for p in path.rglob("*") if p.is_file())


def test_reverse_dns_id_is_prefix_plus_app_id():
    assert GTK_APP_ID == "com.app." + APP_ID
    cmake = _read("CMakeLists.txt")
    assert 'file(STRINGS "${CMAKE_SOURCE_DIR}/identity.env"' in cmake
    assert 'string(CONCAT DLNA_GTK_APP_ID "${DLNA_REVERSE_DNS_PREFIX}" "." "${DLNA_APP_ID}")' in cmake


def test_identity_literals_live_only_in_identity_env():
    for item in _scan_files():
        rel = item.relative_to(ROOT).as_posix()
        if rel in LEGACY_CLEANUP:
            continue
        try:
            text = item.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            continue
        assert APP_ID not in text, rel
        assert "com.github" not in text, rel


def test_packaging_files_are_named_by_role_not_by_id():
    for template in ROLE_TEMPLATES:
        assert (ROOT / template).is_file(), template
    stale = [p.name for p in (ROOT / "packaging").rglob("*") if APP_ID in p.name]
    assert stale == []


def test_metainfo_and_manifest_use_the_app_id_placeholder():
    metainfo = _read("packaging/linux/metainfo.xml.in")
    assert "<id>@DLNA_GTK_APP_ID@</id>" in metainfo
    assert '<launchable type="desktop-id">@DLNA_GTK_APP_ID@.desktop</launchable>' in metainfo
    manifest = _read("packaging/flatpak/manifest.yml.in")
    assert 'app-id: "@DLNA_GTK_APP_ID@"' in manifest
    assert "/app/share/applications/@DLNA_GTK_APP_ID@.desktop" in manifest


def test_header_names_come_from_product_name():
    header = _read("src/app_identity.h.in")
    assert '#define DLNA_GTK_APP_ID "@DLNA_GTK_APP_ID@"' in header
    assert 'L"@DLNA_PRODUCT_NAME@.exe"' in header
    for removed in ("DLNA_WSLG_APP_ID", "DLNA_APP_ICON"):
        assert removed not in header


def test_package_and_asset_names_use_product_name():
    assert 'set(CPACK_PACKAGE_NAME "${DLNA_PRODUCT_NAME}")' in _read("CMakeLists.txt")
    for script in ("scripts/build_linux.sh", "scripts/install_linux.sh"):
        script_text = _read(script)
        assert "${DLNA_PRODUCT_NAME}_*.deb" in script_text
    ps1 = _read("scripts/build-windows.ps1")
    assert "identity.env" in ps1
    assert "$ProductName-$VersionNumber-windows-$Architecture.zip" in ps1
    assert "com.github" not in ps1


def test_build_scripts_stamp_identity_from_one_function():
    assert "stamp_identity()" in _read("scripts/lib_identity.sh")
    for script in ("scripts/build_appimage.sh", "scripts/build_flatpak.sh"):
        assert "stamp_identity" in _read(script)
    assert '"$DLNA_GTK_APP_ID" stable' in _read("scripts/build_flatpak.sh")


def test_conftest_has_no_hardcoded_identity():
    text = _read("tests/conftest.py")
    assert APP_ID not in text
    assert "com.github" not in text
    assert "from tests.identity import" in text


@pytest.mark.posix_only
def test_lib_identity_exports_all_names():
    result = subprocess.run(
        ["bash", "-c",
         'source scripts/lib_identity.sh; printf "%s|%s|%s" "$DLNA_APP_ID" "$DLNA_GTK_APP_ID" "$DLNA_PRODUCT_NAME"'],
        cwd=ROOT, capture_output=True, text=True, timeout=30)
    assert result.stdout == f"{APP_ID}|{GTK_APP_ID}|{PRODUCT_NAME}"
