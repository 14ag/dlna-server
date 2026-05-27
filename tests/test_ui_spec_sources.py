import unittest
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UiSpecTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_ui_spec_covers_cross_platform_contract(self):
        spec = self.read("docs/ui-spec.md")

        for text in (
            "Windows Win32 UI and Linux FLTK UI",
            "Minimum content size: 640 x 460",
            "Toolbar height: 56 px",
            "Status strip height: 40 px",
            "Windows UI font: Segoe UI Variable",
            "Toolbar buttons are 40 x 40 px",
            "8 px corner radius",
            "4 px increments",
            "Add media folder",
            "Start server",
            "Stop server",
            "Settings",
            "Default playlist",
            "Server icon",
            "Log text is read-only",
            "No clipped labels",
        ):
            self.assertIn(text, spec)

    def test_release_version_is_1_3_0(self):
        cmake = self.read("CMakeLists.txt")
        changelog = self.read("CHANGELOG.md")

        self.assertIn("project(dlna-server VERSION 1.3.0)", cmake)
        self.assertIn("## [1.3.0] - 2026-05-24", changelog)

    def test_windows_ui_uses_winui_aligned_metrics_and_icon(self):
        main = self.read("src/mainwindow.cpp")
        app_rc = self.read("resources/app.rc")
        svg = self.read("resources/dlna-server.svg")
        cmake = self.read("CMakeLists.txt")

        for token in (
            "const int kGutter = 24",
            "const int kToolbarHeight = 56",
            "const int kStatusHeight = 40",
            "const int kButtonSize = 40",
            "const int kButtonGap = 8",
            "const int kCornerDiameter = 8",
            'CreateUiFont(20, FW_SEMIBOLD, L"Segoe UI Variable")',
            'CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable")',
            'CreateUiFont(16, FW_NORMAL, L"Segoe MDL2 Assets")',
            "BS_OWNERDRAW",
            "RoundRect",
            "WS_BORDER",
        ):
            self.assertIn(token, main)

        self.assertIn('FONT 9, "Segoe UI Variable"', app_rc)
        self.assertNotIn("DS_FIXEDSYS", app_rc)
        for token in (
            'VALUE "FileDescription", "DLNA Server"',
            'VALUE "InternalName", "DLNA Server"',
            'VALUE "OriginalFilename", "DLNA Server.exe"',
            'VALUE "ProductName", "DLNA Server"',
            'IDI_APP_ICON ICON "app.ico"',
        ):
            self.assertIn(token, app_rc)
        self.assertIn('set_target_properties(dlna-server PROPERTIES OUTPUT_NAME "DLNA Server")', cmake)
        self.assertIn('CreateWindowExW(\n        0, CLASS_NAME, L"DLNA Server"', main)
        self.assertIn('wcscpy_s(nid.szTip, L"DLNA Server")', main)
        self.assertIn('linearGradient id="plate"', svg)
        self.assertIn('filter id="shadow"', svg)

        data = (ROOT / "resources/app.ico").read_bytes()
        self.assertEqual(data[:4], b"\x00\x00\x01\x00")
        count = struct.unpack_from("<H", data, 4)[0]
        entries = data[6:6 + count * 16]
        sizes = set()
        for offset in range(0, len(entries), 16):
            width = entries[offset] or 256
            height = entries[offset + 1] or 256
            sizes.add((width, height))
        self.assertTrue({(16, 16), (24, 24), (32, 32), (48, 48), (256, 256)}.issubset(sizes))


if __name__ == "__main__":
    unittest.main()
