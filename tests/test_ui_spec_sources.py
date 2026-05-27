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
            "Windows UI font: Segoe UI Variable with Segoe UI fallback",
            "Windows dialog templates use 10 pt Segoe UI",
            "Parent surfaces own layout",
            "Toolbar buttons are 32 px tall",
            "8 px corner radius",
            "4 px increments",
            "Add media folder",
            "Delete selected source",
            "Start server",
            "Stop server",
            "Settings",
            "Default playlist",
            "UPnP device icons",
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
            "const int kGutter = 16",
            "const int kToolbarHeight = 56",
            "const int kStatusHeight = 40",
            "const int kButtonHeight = 32",
            "const int kButtonGap = 8",
            "const int kCornerDiameter = 8",
            'CreateUiFont(20, FW_SEMIBOLD, L"Segoe UI Variable Display")',
            'CreateUiFont(14, FW_NORMAL, L"Segoe UI Variable Text")',
            "DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE",
            'CreateWindowExW(0, L"BUTTON", L"Add"',
            'CreateWindowExW(0, L"BUTTON", L"Delete"',
            'CreateWindowExW(0, L"BUTTON", L"Start"',
            'CreateWindowExW(0, L"BUTTON", L"Settings"',
            "IDC_BTN_DELETE",
            "RemoveSelectedSource",
            "VK_DELETE",
            "LBN_SELCHANGE",
            "BS_OWNERDRAW",
            "RoundRect",
            "WS_BORDER",
        ):
            self.assertIn(token, main)

        self.assertIn("dwmapi", cmake)
        settings = self.read("src/settingsdlg.cpp")
        logdlg = self.read("src/logdlg.cpp")
        self.assertNotIn("SetWindowTheme", settings)
        self.assertNotIn("WM_CTLCOLORDLG", settings + logdlg)
        self.assertNotIn("BS_OWNERDRAW", settings + logdlg)
        self.assertIn("PlaylistEntryProc", settings)

        self.assertIn('FONT 10, "Segoe UI"', app_rc)
        self.assertIn("IDD_SETTINGS DIALOGEX 0, 0, 400, 326", app_rc)
        self.assertIn("EDITTEXT        IDC_EDT_SERVER_NAME,110,26,190,19", app_rc)
        self.assertIn('CreateScaledFont(hwnd, 14, FW_NORMAL, L"Segoe UI Variable Text")', main)
        self.assertIn('CreateScaledFont(hwnd, 14, FW_NORMAL, L"Segoe UI Variable Text")', settings)
        self.assertIn("PostQuitMessage(static_cast<int>(msg.wParam))", main)
        self.assertIn("PostQuitMessage(static_cast<int>(msg.wParam))", settings)
        self.assertIn("ApplyDarkFrame(hwndDlg)", settings)
        self.assertIn("ApplyDarkFrame(hwndDlg)", logdlg)
        self.assertIn("ApplyDialogFont(hwndDlg)", settings)
        self.assertIn("EnumChildWindows(hwnd, SetChildFontProc", settings)
        for token in (
            "const int kLabelToControlGap = 12",
            "const int kRelatedControlGap = 8",
            "const int kControlHeight = 32",
            "kPlaylistEditWidth = kPlaylistDialogWidth",
        ):
            self.assertIn(token, settings)
        for token in (
            "const int kSourcePromptContentWidth = kSourcePromptWidth - (kGutter * 2)",
            "const int kSourcePromptEditTop = kGutter + kSourcePromptLabelHeight + 12",
        ):
            self.assertIn(token, main)
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
