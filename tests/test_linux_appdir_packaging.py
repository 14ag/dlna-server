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
        self.assertIn("StartupWMClass=com.github.dlna-server-14ag", desktop)

    def test_linux_desktop_installers_are_scripted(self):
        cmake = self.read("CMakeLists.txt")
        gitattributes = self.read(".gitattributes")
        flatpak = self.read("packaging/flatpak/com.github.14ag.dlna_server.yml")
        desktop = self.read("packaging/flatpak/com.github.14ag.dlna_server.desktop")

        self.assertIn('set(CPACK_GENERATOR "DEB")', cmake)
        self.assertIn("CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON", cmake)
        self.assertIn("*.sh text eol=lf", gitattributes)
        self.assertIn("*.yml text eol=lf", gitattributes)
        self.assertIn("app-id: com.github.14ag.dlna_server", flatpak)
        self.assertIn("--share=network", flatpak)
        self.assertIn("--filesystem=home", flatpak)
        self.assertIn("Name=DLNA Server", desktop)

    def test_macos_dmg_packaging_prefers_native_gui(self):
        cmake = self.read("CMakeLists.txt")
        plist = self.read("packaging/macos/Info.plist.in")
        launcher = self.read("packaging/macos/dlna-server-gui")

        self.assertIn("DLNA Server.app", cmake)
        self.assertIn("dlna-server-gui-bin", cmake)
        self.assertIn("DLNA_PLATFORM_NAME=\\\"macOS\\\"", cmake)
        self.assertIn("<string>DLNA Server</string>", plist)
        self.assertIn('native_gui="$app_dir/Contents/MacOS/dlna-server-gui-bin"', launcher)
        self.assertIn("Rebuild with -DDLNA_ENABLE_GTK4_GUI=ON.", launcher)
        self.assertNotIn("posix_gui.py", launcher)

    def test_wslg_gui_smoke_prereqs_are_in_build_and_pytest_wiring(self):
        conftest = self.read("tests/conftest.py")
        self.assertIn("DLNA_GUI_BINARY", conftest)

    def test_gtk4_gui_is_only_posix_desktop_ui(self):
        cmake = self.read("CMakeLists.txt")
        gui_source = self.read("src/gtk4_gui_main.cpp")
        linux_launcher = self.read("packaging/linux/dlna-server-gui")
        mac_launcher = self.read("packaging/macos/dlna-server-gui")

        self.assertIn("DLNA_ENABLE_GTK4_GUI", cmake)
        self.assertIn('option(DLNA_ENABLE_GTK4_GUI "Build the native GTK4 Linux GUI" ON)', cmake)
        self.assertNotIn("DLNA_ENABLE_FLTK_GUI", cmake)
        self.assertNotIn("find_package(FLTK QUIET)", cmake)
        self.assertNotIn("dlna-server-gui-native", cmake)
        self.assertNotIn("fltk_gui_main.cpp", cmake)
        self.assertIn("pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)", cmake)
        self.assertIn("dlna-server-gui-gtk4", cmake)
        self.assertIn("OUTPUT_NAME dlna-server-gui-bin", cmake)
        self.assertIn('configure_file(packaging/linux/dlna-server-gui "${CMAKE_BINARY_DIR}/dlna-server-gui" @ONLY NEWLINE_STYLE UNIX)', cmake)
        self.assertIn('install(PROGRAMS "${CMAKE_BINARY_DIR}/dlna-server-gui"', cmake)
        self.assertIn("src/posix_server.cpp", cmake)
        self.assertIn("Threads::Threads", cmake)
        self.assertIn("#include <gtk/gtk.h>", gui_source)
        self.assertNotIn("DLNA Server is stopped", gui_source)
        self.assertFalse((ROOT / "src/posix_gui.py").exists())
        self.assertFalse((ROOT / "tests/test_posix_gui.py").exists())
        self.assertNotIn("tkinter", linux_launcher + mac_launcher + cmake)
        self.assertNotIn("python3", linux_launcher + mac_launcher + cmake)
        self.assertIn("DLNA Server native GUI is missing", linux_launcher)

    def test_gtk4_main_window_has_parity_controls(self):
        gui_source = self.read("src/gtk4_gui_main.cpp")
        tokens_h = self.read("src/ui_tokens.h")

        self.assertIn("GtkListBox", gui_source)
        self.assertIn("GtkFileChooserNative", gui_source)
        self.assertIn("Add media source", gui_source)
        self.assertIn("Delete selected source", gui_source)
        self.assertIn('"Delete"', gui_source)
        self.assertIn("RemoveSelectedSource", gui_source)
        self.assertIn("Start server", gui_source)
        self.assertIn("Stop server", gui_source)
        self.assertIn('"Settings"', gui_source)
        self.assertIn("Please add shared folders or files", gui_source)
        self.assertIn("constexpr int kWindowWidth = 440", tokens_h)
        self.assertIn("constexpr int kWindowHeight = 600", tokens_h)
        self.assertIn("constexpr int kToolbarHeight = 56", tokens_h)
        self.assertIn("constexpr int kStatusHeight = 40", tokens_h)
        self.assertIn("constexpr int kListTopGap = 8", tokens_h)
        self.assertIn("gtk_application_window_new", gui_source)
        self.assertIn("DLNAServer.Start(", gui_source)
        self.assertIn("DLNAServer.Stop()", gui_source)
        self.assertIn("AppConfig.Save()", gui_source)
        self.assertIn("DLNAServer.Rescan()", gui_source)


if __name__ == "__main__":
    unittest.main()
