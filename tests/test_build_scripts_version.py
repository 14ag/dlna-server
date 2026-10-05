from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_ID = "com.github.dlna-server-14ag"


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_identity_library():
    text = read("scripts/lib_identity.sh")
    assert f'DLNA_APP_ID="{APP_ID}"' in text
    assert "resolve_version()" in text
    assert "--numeric-of" in text


def test_build_sh_resolves_and_exports_version():
    text = read("build.sh")
    assert "--version=*)" in text
    assert "resolve_version" in text
    assert text.index("resolve_version") > text.index('if [ "$RELEASE" = "1" ]')


def test_build_linux_passes_version_to_cmake():
    text = read("scripts/build_linux.sh")
    assert '-DDLNA_VERSION="$DLNA_VERSION"' in text
    assert "resolve_version" in text
    assert "dlna-server_*.deb" not in text
    assert "${DLNA_APP_ID}_*.deb" in text


def test_install_script_handles_old_and_new_package_name():
    text = read("scripts/install_linux.sh")
    assert "dlna-server_*.deb" not in text
    assert "${DLNA_APP_ID}_*.deb" in text
    assert 'dpkg -P "$DLNA_APP_ID"' in text
    assert "dpkg -P dlna-server " in text


def test_build_bat_generates_and_forwards_version():
    text = read("build.bat")
    assert 'set "VERSION_TAG="' in text
    assert "scripts\\version.py" in text
    assert "for /f" in text
    assert "DLNA_VERSION_TAG='%VERSION_TAG%'" in text
    assert '-Version "%VERSION_TAG%"' in text
    assert '"%ARG:~0,10%"=="--version="' in text
    assert 'if /I "%~1"=="--version"' in text


def test_windows_script_receives_version_and_brands_zip():
    text = read("scripts/build-windows.ps1")
    assert '[string]$Version = ""' in text
    assert "-DDLNA_VERSION=$VersionNumber" in text
    assert "Could not read version" not in text
    assert "$AppId-$VersionNumber-windows-$Architecture.zip" in text
    assert f'$AppId = "{APP_ID}"' in text
    assert all(ord(c) < 128 for c in text)


def test_app_id_is_identical_in_every_script_and_cmake():
    for rel in ["CMakeLists.txt", "scripts/lib_identity.sh", "scripts/build-windows.ps1"]:
        assert APP_ID in read(rel), rel


def test_appimage_script_is_branded_and_version_driven():
    text = read("scripts/build_appimage.sh")
    assert "project\\(dlna-server VERSION" not in text
    assert "resolve_version" in text
    assert "dlna-server.appimage.desktop" not in text
    assert "dlna-server.svg" not in text
    assert "DLNA_Server-" not in text
    assert "${DLNA_APP_ID}.appimage.desktop" in text


def test_flatpak_script_stamps_version_and_uses_shared_template():
    text = read("scripts/build_flatpak.sh")
    assert "project\\(dlna-server VERSION" not in text
    assert ".stamped.yml" in text
    assert "packaging/flatpak/com.github.dlna-server-14ag.metainfo.xml" not in text
    assert "@PROJECT_VERSION@" in text
    assert "@DLNA_RELEASE_DATE@" in text
    assert "${DLNA_APP_ID}-${version}-linux-x86_64.flatpak" in text
