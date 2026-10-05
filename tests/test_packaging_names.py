from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_ID = "com.github.dlna-server-14ag"
LINUX = ROOT / "packaging" / "linux"


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_branded_files_exist():
    assert (LINUX / f"{APP_ID}.appdata.xml").is_file()
    assert (LINUX / f"{APP_ID}.appimage.desktop").is_file()
    assert (ROOT / "resources" / f"{APP_ID}.svg").is_file()


def test_old_names_are_gone():
    for rel in ["packaging/linux/dlna-server.appdata.xml.in",
                "packaging/linux/dlna-server.appimage.desktop",
                "resources/dlna-server.svg",
                "packaging/flatpak/com.github.dlna-server-14ag.metainfo.xml"]:
        assert not (ROOT / rel).exists(), rel


def test_appdata_template_uses_placeholders():
    text = (LINUX / f"{APP_ID}.appdata.xml").read_text(encoding="utf-8")
    assert "<id>@DLNA_APP_ID@</id>" in text
    assert 'version="@PROJECT_VERSION@"' in text
    assert 'date="@DLNA_RELEASE_DATE@"' in text


def test_appimage_desktop_icon_matches_app_id():
    text = (LINUX / f"{APP_ID}.appimage.desktop").read_text(encoding="utf-8")
    assert f"Icon={APP_ID}" in text
    assert "Exec=dlna-server-gui" in text


def test_cmake_uses_branded_names_and_keeps_startup_commands():
    text = read("CMakeLists.txt")
    assert "packaging/linux/${DLNA_APP_ID}.appdata.xml" in text
    assert "resources/${DLNA_APP_ID}.svg" in text
    assert 'set(CPACK_PACKAGE_NAME "${DLNA_APP_ID}")' in text
    assert 'set(CPACK_DEBIAN_PACKAGE_REPLACES "dlna-server")' in text
    assert 'set(CPACK_DEBIAN_PACKAGE_CONFLICTS "dlna-server")' in text
    assert "RENAME dlna-server" not in text
    assert "install(TARGETS dlna-server" in text
    assert 'install(PROGRAMS "${CMAKE_BINARY_DIR}/dlna-server-gui"' in text


def test_install_templates_use_app_id():
    desktop = read("packaging/linux/install_desktop.cmake.in")
    assert "@DLNA_APP_ID@.desktop" in desktop
    assert "Icon=@DLNA_APP_ID@" in desktop
    assert "StartupWMClass=@DLNA_APP_ID@" in desktop
    assert "@DLNA_APP_ID@.desktop" in read("packaging/linux/postinst.in")
    assert "gtk-launch @DLNA_APP_ID@" in read("packaging/linux/dlna-server-gui")


def test_flatpak_manifest_is_a_version_template():
    text = read("packaging/flatpak/com.github.dlna-server-14ag.yml")
    assert "-DDLNA_VERSION=@PROJECT_VERSION@" in text
    assert "rm -f /app/share/applications" not in text
    assert "resources/dlna-server.svg" not in text
