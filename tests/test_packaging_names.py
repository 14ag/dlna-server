from pathlib import Path

from tests.identity import APP_ID

ROOT = Path(__file__).resolve().parent.parent
LINUX = ROOT / "packaging" / "linux"


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_branded_files_exist():
    assert (LINUX / "metainfo.xml.in").is_file()
    assert (LINUX / "portable.desktop.in").is_file()
    assert (ROOT / "packaging" / "flatpak" / "manifest.yml.in").is_file()
    assert (ROOT / "resources" / f"{APP_ID}.svg").is_file()


def test_old_names_are_gone():
    for rel in ["packaging/linux/dlna-server.appdata.xml.in",
                "packaging/linux/dlna-server.appimage.desktop",
                "resources/dlna-server.svg",
                f"packaging/flatpak/{APP_ID}.metainfo.xml",
                f"packaging/linux/{APP_ID}.appdata.xml",
                f"packaging/linux/{APP_ID}.appimage.desktop",
                f"packaging/flatpak/{APP_ID}.yml",
                f"packaging/flatpak/{APP_ID}.desktop"]:
        assert not (ROOT / rel).exists(), rel


def test_appdata_template_uses_placeholders():
    text = (LINUX / "metainfo.xml.in").read_text(encoding="utf-8")
    assert "<id>@DLNA_GTK_APP_ID@</id>" in text
    assert 'version="@PROJECT_VERSION@"' in text
    assert 'date="@DLNA_RELEASE_DATE@"' in text


def test_appimage_desktop_icon_matches_app_id():
    text = (LINUX / "portable.desktop.in").read_text(encoding="utf-8")
    assert "Icon=@DLNA_GTK_APP_ID@" in text
    assert "Exec=dlna-server-gui" in text


def test_cmake_uses_branded_names_and_keeps_startup_commands():
    text = read("CMakeLists.txt")
    assert "packaging/linux/metainfo.xml.in" in text
    assert "resources/${DLNA_APP_ID}.svg" in text
    assert 'set(CPACK_PACKAGE_NAME "${DLNA_PRODUCT_NAME}")' in text
    assert 'set(CPACK_DEBIAN_PACKAGE_REPLACES "${DLNA_APP_ID}")' in text
    assert 'set(CPACK_DEBIAN_PACKAGE_CONFLICTS "${DLNA_APP_ID}")' in text
    assert "RENAME dlna-server" not in text
    assert "install(TARGETS dlna-server" in text
    assert 'install(PROGRAMS "${CMAKE_BINARY_DIR}/dlna-server-gui"' in text


def test_install_templates_use_app_id():
    desktop = read("packaging/linux/install_desktop.cmake.in")
    assert "@DLNA_GTK_APP_ID@.desktop" in desktop
    assert "Icon=@DLNA_GTK_APP_ID@" in desktop
    assert "StartupWMClass=@DLNA_GTK_APP_ID@" in desktop
    assert "Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui" in desktop
    assert "@DLNA_GTK_APP_ID@.desktop" in read("packaging/linux/postinst.in")
    assert 'exec "$native_gui" "$@"' in read("packaging/linux/dlna-server-gui")


def test_flatpak_manifest_is_a_version_template():
    text = read("packaging/flatpak/manifest.yml.in")
    assert "-DDLNA_VERSION=@PROJECT_VERSION@" in text
    assert "rm -f /app/share/applications" not in text
    assert "resources/dlna-server.svg" not in text
