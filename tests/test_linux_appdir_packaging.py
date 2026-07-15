import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LinuxAppDirPackagingTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_apprun_launches_gui_with_bundled_server(self):
        apprun = self.read("packaging/linux/AppRun")

        self.assertIn('DLNA_SERVER_BIN="$appdir/usr/bin/dlna-server"', apprun)
        self.assertIn('DLNA_SERVER_GUI_BIN="$appdir/usr/bin/dlna-server-gui-bin"', apprun)
        self.assertIn('exec "$appdir/usr/bin/dlna-server-gui"', apprun)
        self.assertNotIn("DLNA_SERVER_GUI_DIR", apprun)

    def test_appdir_desktop_metadata_is_relative(self):
        desktop = self.read("packaging/linux/dlna-server.appimage.desktop")

        self.assertIn("Name=DLNA Server", desktop)
        self.assertIn("Exec=dlna-server-gui", desktop)
        self.assertIn("Icon=dlna-server", desktop)
        self.assertIn("StartupWMClass=dlna-server", desktop)

    def test_linux_desktop_installers_are_scripted(self):
        cmake = self.read("CMakeLists.txt")
        script = self.read("scripts/build-linux-desktop-assets.sh")
        gitattributes = self.read(".gitattributes")
        flatpak = self.read("packaging/flatpak/com.github.14ag.dlna_server.yml")
        desktop = self.read("packaging/flatpak/com.github.14ag.dlna_server.desktop")
        workflow = self.read(".github/workflows/release-assets.yml")

        self.assertIn('set(CPACK_GENERATOR "DEB")', cmake)
        self.assertIn("CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON", cmake)
        self.assertIn("cpack --config", script)
        self.assertIn("linuxdeploy-x86_64.AppImage", script)
        self.assertIn("linuxdeploy_version=\"1-alpha-20251107-1\"", script)
        self.assertIn("linuxdeploy_sha256=", script)
        self.assertIn("download_verified", script)
        self.assertIn("-name '*.AppImage'", script)
        self.assertIn('flatpak-builder and flatpak are required for default release assets', script)
        self.assertIn("*.sh text eol=lf", gitattributes)
        self.assertIn("*.yml text eol=lf", gitattributes)
        self.assertIn("build-bundle", script)
        self.assertIn("app-id: com.github.14ag.dlna_server", flatpak)
        self.assertIn("--share=network", flatpak)
        self.assertIn("--filesystem=home", flatpak)
        self.assertIn("Name=DLNA Server", desktop)
        self.assertIn("Release assets", workflow)
        self.assertIn("runs-on: windows-latest", workflow)
        self.assertIn("output/winx64/*.zip", workflow)
        self.assertIn("output/winx86/*.zip", workflow)
        self.assertIn("flatpak flatpak-builder", workflow)
        self.assertIn("org.freedesktop.Sdk//24.08", workflow)
        self.assertIn("scripts/build-linux-desktop-assets.sh", workflow)

    def test_macos_dmg_packaging_prefers_native_gui(self):
        cmake = self.read("CMakeLists.txt")
        plist = self.read("packaging/macos/Info.plist.in")
        launcher = self.read("packaging/macos/dlna-server-gui")
        script = self.read("scripts/build-macos-dmg.sh")

        self.assertIn("DLNA Server.app", cmake)
        self.assertIn("dlna-server-gui-bin", cmake)
        self.assertIn("DLNA_PLATFORM_NAME=\\\"macOS\\\"", cmake)
        self.assertIn("<string>DLNA Server</string>", plist)
        self.assertIn('native_gui="$app_dir/Contents/MacOS/dlna-server-gui-bin"', launcher)
        self.assertIn("Rebuild with -DDLNA_ENABLE_FLTK_GUI=ON.", launcher)
        self.assertNotIn("posix_gui.py", launcher)
        self.assertIn("hdiutil create", script)
        self.assertIn("notarytool submit", script)

    def test_wslg_gui_smoke_script_checks_native_deps(self):
        script = self.read("tests/verify-wslg-gui.ps1")

        self.assertIn("DLNA_ENABLE_FLTK_GUI=ON", script)
        self.assertIn("libx11-dev", script)
        self.assertIn("WAYLAND_DISPLAY", script)
        self.assertIn("dlna-server-gui", script)
        self.assertIn("PASS WSLg native GUI launch smoke", script)

    def test_fltk_gui_is_only_posix_desktop_ui(self):
        cmake = self.read("CMakeLists.txt")
        gui_source = self.read("src/fltk_gui_main.cpp")
        linux_launcher = self.read("packaging/linux/dlna-server-gui")
        mac_launcher = self.read("packaging/macos/dlna-server-gui")

        self.assertIn("DLNA_ENABLE_FLTK_GUI", cmake)
        self.assertIn('option(DLNA_ENABLE_FLTK_GUI "Build the native FLTK Linux GUI" ON)', cmake)
        self.assertIn("find_package(FLTK QUIET)", cmake)
        self.assertIn("FetchContent_Declare", cmake)
        self.assertIn("EXCLUDE_FROM_ALL", cmake)
        self.assertIn("release-1.4.5", cmake)
        self.assertIn("dlna-server-gui-native", cmake)
        self.assertIn("if(FLTK_FOUND)", cmake)
        self.assertIn("OUTPUT_NAME dlna-server-gui-bin", cmake)
        self.assertIn('configure_file(packaging/linux/dlna-server-gui "${CMAKE_BINARY_DIR}/dlna-server-gui" @ONLY NEWLINE_STYLE UNIX)', cmake)
        self.assertIn('install(PROGRAMS "${CMAKE_BINARY_DIR}/dlna-server-gui"', cmake)
        self.assertIn("src/posix_server.cpp", cmake)
        self.assertIn("Threads::Threads", cmake)
        self.assertIn("#include <FL/Fl_Window.H>", gui_source)
        self.assertIn("DLNA Server is stopped", gui_source)
        self.assertFalse((ROOT / "src/posix_gui.py").exists())
        self.assertFalse((ROOT / "tests/test_posix_gui.py").exists())
        self.assertNotIn("tkinter", linux_launcher + mac_launcher + cmake)
        self.assertNotIn("python3", linux_launcher + mac_launcher + cmake)
        self.assertIn("DLNA Server native GUI is missing", linux_launcher)

    def test_fltk_main_window_has_parity_controls(self):
        gui_source = self.read("src/fltk_gui_main.cpp")

        self.assertIn("class MainWindow", gui_source)
        self.assertIn("Fl_Hold_Browser", gui_source)
        self.assertIn("Fl_Native_File_Chooser", gui_source)
        self.assertIn("Add media source", gui_source)
        self.assertIn("Delete selected source", gui_source)
        self.assertIn('"Delete"', gui_source)
        self.assertIn("FL_Delete", gui_source)
        self.assertIn("RemoveSelectedSource", gui_source)
        self.assertIn("Start server", gui_source)
        self.assertIn("Stop server", gui_source)
        self.assertIn("Settings", gui_source)
        self.assertIn("Please add shared folders or files", gui_source)
        self.assertIn("constexpr int kWindowWidth = 440", gui_source)
        self.assertIn("constexpr int kWindowHeight = 600", gui_source)
        self.assertIn("constexpr int kToolbarHeight = 56", gui_source)
        self.assertIn("constexpr int kStatusHeight = 40", gui_source)
        self.assertIn("constexpr int kListTopGap = 8", gui_source)
        self.assertIn("size_range(440, 460)", gui_source)
        self.assertIn("Fl::focus(&m_sources)", gui_source)
        self.assertIn("Fl::focus(state->movie)", gui_source)
        self.assertIn("Fl::focus(state->subtitle)", gui_source)
        self.assertIn("Fl::focus(&dialog)", gui_source)
        self.assertIn("void resize", gui_source)
        self.assertIn("DLNAServer.Start()", gui_source)
        self.assertIn("DLNAServer.Stop()", gui_source)
        self.assertIn("AppConfig.Save()", gui_source)
        self.assertIn("DLNAServer.Rescan()", gui_source)

    
if __name__ == "__main__":
    unittest.main()
