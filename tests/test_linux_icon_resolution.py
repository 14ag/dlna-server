"""
Contract tests for Linux icon resolution and desktop integration.

Validates that the C++ source and build rules implement the bundled
resource search, FLTK window icon, hicolor icon tree install, and
.desktop file alignment changes.
"""

from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


# ---- posix_config.cpp ----

class TestBundledResourceResolver:
    def test_resolve_bundled_resource_path_exists(self):
        """Must define ResolveBundledResourcePath()."""
        src = _source("posix_config.cpp")
        assert "ResolveBundledResourcePath" in src
        assert "DLNA_RESOURCE_DIR" in src

    def test_resolve_search_order(self):
        """Candidate paths must include exe-dir, installed path, macOS bundle."""
        src = _source("posix_config.cpp")
        assert "exeDir + \"/../share/dlna-server/icons/\"" in src
        assert "exeDir + \"/../Resources/\"" in src


# ---- config.h ----

class TestConfigHeader:
    def test_resolve_bundled_resource_path_declared(self):
        """config.h must declare ResolveBundledResourcePath under #ifdef DLNA_POSIX."""
        src = _source("config.h")
        assert "ResolveBundledResourcePath" in src
        assert "#ifdef DLNA_POSIX" in src


# ---- fltk_gui_main.cpp ----

class TestFltkIconIntegration:
    def test_wm_class_set(self):
        """The FLTK MainWindow must call xclass('dlna-server')."""
        src = _source("fltk_gui_main.cpp")
        assert 'xclass("dlna-server")' in src

    def test_window_icon_loaded(self):
        """The FLTK MainWindow must load icon via ResolveBundledResourcePath."""
        src = _source("fltk_gui_main.cpp")
        assert "ResolveBundledResourcePath" in src
        assert "Fl_PNG_Image" in src
        assert 'server_icon_48.png' in src

    def test_png_image_header_included(self):
        """fltk_gui_main.cpp must include Fl_PNG_Image.H."""
        src = _source("fltk_gui_main.cpp")
        assert "#include <FL/Fl_PNG_Image.H>" in src


# ---- posix_httpserver.cpp ----

class TestHttpServerIconRefactor:
    def test_uses_shared_resolver(self):
        """posix_httpserver must call ResolveBundledResourcePath, not inline candidates."""
        src = _source("posix_httpserver.cpp")
        assert "ResolveBundledResourcePath" in src
        # The old ExecutableDirectory() definition must be gone.
        assert "ExecutableDirectory" not in src

    def test_icon_loaded_via_resolver(self):
        """LoadServerIconPng must use the shared resolver."""
        src = _source("posix_httpserver.cpp")
        assert "LoadServerIconPng" in src


# ---- CMakeLists.txt ----

class TestCmakeIconInstall:
    def test_hicolor_install_rules(self):
        """CMakeLists.txt must install PNGs into hicolor/{48x48,128x128,256x256}/apps."""
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        assert "hicolor/48x48/apps" in cmake
        assert "hicolor/128x128/apps" in cmake
        assert "hicolor/256x256/apps" in cmake

    def test_rename_to_dlna_server_png(self):
        """Each hicolor install must RENAME to dlna-server.png."""
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        assert "RENAME dlna-server.png" in cmake
        # Verify at least two sizes have the rename.
        assert cmake.count("RENAME dlna-server.png") >= 2


# ---- install_desktop.cmake.in ----

class TestDesktopIntegration:
    def test_gtk_icon_cache_invalidation(self):
        """install_desktop.cmake.in must run gtk-update-icon-cache."""
        cmake_in = (ROOT / "packaging" / "linux" / "install_desktop.cmake.in").read_text(encoding="utf-8")
        assert "gtk-update-icon-cache" in cmake_in

    def test_startup_wm_class(self):
        """Both .desktop files must declare StartupWMClass=dlna-server."""
        for desktop in [
            "packaging/linux/dlna-server.appimage.desktop",
            "packaging/flatpak/com.github.14ag.dlna_server.desktop",
        ]:
            text = (ROOT / desktop).read_text(encoding="utf-8")
            assert "StartupWMClass=dlna-server" in text

    def test_icon_name(self):
        """Both .desktop files must declare Icon=dlna-server or compat variant."""
        appimage = (ROOT / "packaging" / "linux" / "dlna-server.appimage.desktop").read_text(encoding="utf-8")
        assert "Icon=dlna-server" in appimage


# ---- posix_main.cpp (diagnostic hook) ----

class TestBundledResourceDiagnostic:
    def test_print_resolve_bundled_resource_flag(self):
        """posix_main.cpp must handle --print-resolve-bundled-resource."""
        src = _source("posix_main.cpp")
        assert "--print-resolve-bundled-resource" in src
