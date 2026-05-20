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


if __name__ == "__main__":
    unittest.main()
