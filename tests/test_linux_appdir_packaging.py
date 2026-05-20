import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LinuxAppDirPackagingTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_appdir_script_builds_expected_layout(self):
        script = self.read("build-linux-appdir.ps1")

        for required_path in (
            "usr/bin/dlna-server",
            "usr/bin/dlna-server-gui",
            "usr/share/dlna-server",
            "usr/share/icons/hicolor/scalable/apps/dlna-server.svg",
            "AppRun",
            "dlna-server.desktop",
        ):
            self.assertIn(required_path, script)

        self.assertIn("Resolve-WorkspacePath", script)
        self.assertIn("AppDir validation failed", script)

    def test_apprun_launches_gui_with_bundled_server(self):
        apprun = self.read("packaging/linux/AppRun")

        self.assertIn('DLNA_SERVER_BIN="$appdir/usr/bin/dlna-server"', apprun)
        self.assertIn('DLNA_SERVER_GUI_DIR="$appdir/usr/share/dlna-server"', apprun)
        self.assertIn('exec "$appdir/usr/bin/dlna-server-gui"', apprun)

    def test_appdir_desktop_metadata_is_relative(self):
        script = self.read("build-linux-appdir.ps1")

        self.assertIn("Name=DLNA Server", script)
        self.assertIn("Exec=dlna-server-gui", script)
        self.assertIn("Icon=dlna-server", script)

    def test_appimage_script_uses_linuxdeploy(self):
        script = self.read("build-linux-appimage.ps1")

        self.assertIn("build-output.ps1", script)
        self.assertIn("build-linux-appdir.ps1", script)
        self.assertIn("Get-Command $name -ErrorAction SilentlyContinue", script)
        self.assertIn("--appdir", script)
        self.assertIn("--output appimage", script)
        self.assertIn("*.AppImage", script)

    def test_fltk_gui_dependency_is_optional(self):
        cmake = self.read("CMakeLists.txt")
        gui_source = self.read("src/fltk_gui_main.cpp")

        self.assertIn("DLNA_ENABLE_FLTK_GUI", cmake)
        self.assertIn("find_package(FLTK REQUIRED)", cmake)
        self.assertIn("dlna-server-gui-native", cmake)
        self.assertIn("OUTPUT_NAME dlna-server-gui", cmake)
        self.assertIn("packaging/linux/dlna-server-gui", cmake)
        self.assertIn("#include <FL/Fl_Window.H>", gui_source)
        self.assertIn("DLNA Server is stopped", gui_source)

    def test_fltk_main_window_has_parity_controls(self):
        gui_source = self.read("src/fltk_gui_main.cpp")

        self.assertIn("class MainWindow", gui_source)
        self.assertIn("Fl_Hold_Browser", gui_source)
        self.assertIn("Add media folder", gui_source)
        self.assertIn("Start server", gui_source)
        self.assertIn("Stop server", gui_source)
        self.assertIn("Settings", gui_source)
        self.assertIn("Please add shared folders or files", gui_source)
        self.assertIn("size_range(640, 460)", gui_source)
        self.assertIn("void resize", gui_source)

    def test_fltk_settings_and_log_dialogs_have_parity_controls(self):
        gui_source = self.read("src/fltk_gui_main.cpp")

        for label in (
            "DLNA Server Settings",
            "Server Name:",
            "HTTP Port:",
            "File Port:",
            "IP Whitelist:",
            "Run on Windows Startup",
            "Debug Log (Write to file)",
            "Add Artist/Album folders to audio",
            "Do not show 'All Media' folders",
            "Flat folders style",
            "Show file names instead of titles",
            "Sort by title instead of file name",
            "Proxy streams",
            "Show video thumbnails",
            "Show audio album art",
            "Show image thumbnails",
            "Thumbnail quality:",
            "Restart",
            "View log",
            "OK",
            "Cancel",
            "DLNA Server Log",
            "Close",
        ):
            self.assertIn(label, gui_source)

        self.assertIn("Fl_Multiline_Output", gui_source)
        self.assertIn("deactivate()", gui_source)


if __name__ == "__main__":
    unittest.main()
