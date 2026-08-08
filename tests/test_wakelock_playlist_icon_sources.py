import unittest
import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WakeLockPlaylistIconSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_windows_ui_uses_busy_state_and_wake_lock(self):
        header = self.read("src/mainwindow.h")
        source = self.read("src/mainwindow.cpp")

        for token in (
            "enum class ServerUiState",
            "Starting",
            "Stopping",
            "BeginStartServer",
            "BeginStopServer",
            "BeginRestartServer",
            "starting server...",
            "stopping server...",
            "SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)",
            "SetThreadExecutionState(ES_CONTINUOUS)",
            "EnableWindow(m_hBtnStartStop, enableStartStop)",
        ):
            self.assertIn(token, header + source)

        self.assertNotIn("ES_AWAYMODE_REQUIRED", source)
        self.assertNotIn("m_isRunning", header + source)

    def test_settings_generate_m3u_and_playlist_browse_controls(self):
        for path in ("src/settingsdlg.cpp", "src/fltk_gui_main.cpp"):
            source = self.read(path)
            for token in (
                "#EXTM3U",
                "#EXTINF:-1,",
                "#DLNA-SUBTITLE:",
                "#EXTVLCOPT:sub-file=",
                "Default playlist",
                "Browse...",
            ):
                self.assertIn(token, source)

        windows_settings = self.read("src/settingsdlg.cpp")
        self.assertIn("IDC_PLAYLIST_MOVIE_BROWSE", windows_settings)
        self.assertIn("IDC_PLAYLIST_SUBTITLE_BROWSE", windows_settings)
        self.assertIn("IFileOpenDialog", windows_settings)

        fltk_settings = self.read("src/fltk_gui_main.cpp")
        self.assertIn("Fl_Native_File_Chooser", fltk_settings)

    def test_device_description_and_http_servers_expose_icon(self):
        content = self.read("src/contentdirectory.cpp")
        self.assertIn("<iconList>", content)
        for size in ("48", "120", "256"):
            self.assertIn(f"<width>{size}</width>", content)
            self.assertIn(f"<height>{size}</height>", content)
            self.assertIn(f"/icons/server_icon_{size}.png", content)
        self.assertIn("<depth>24</depth>", content)

        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(path)
            self.assertIn('"/icons/server_icon_48.png"', source)
            self.assertIn('"/icons/server_icon_120.png"', source)
            self.assertIn('"/icons/server_icon_256.png"', source)
            self.assertIn("LoadServerIconPng", source)
            self.assertIn("image/png", source)

        cmake = self.read("CMakeLists.txt")
        posix_http = self.read("src/posix_httpserver.cpp")
        self.assertGreaterEqual(cmake.count('DLNA_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"'), 2)
        self.assertIn('ResolveBundledResourcePath(fileName)', posix_http)
        self.assertNotIn('std::string(DLNA_RESOURCE_DIR) + "/" + fileName', posix_http)

        resources = self.read("resources/resource.h")
        app_rc = self.read("resources/app.rc")
        for size in ("48", "120", "256"):
            self.assertIn(f"IDR_SERVER_ICON_{size}", resources)
            self.assertIn(f'IDR_SERVER_ICON_{size} RCDATA "server_icon_{size}.png"', app_rc)

    def test_icon_assets_are_generated_from_current_transparent_source(self):
        expected_sha256 = {
            "resources/app.ico": "532515145f7c51f5cdf58aa0e5a589c641e2bb7bd61fb15a7904583bd7883e85",
            "resources/server_icon_48.png": "c1af99b456c1052850d702b285041e864640ba1f07024f47058efe086e030646",
            "resources/server_icon_120.png": "e7f8ab23a8218707812c105c8c293bc6c0791cd27c8bfaf7c754852c1361ef31",
            "resources/server_icon_256.png": "1f0783a7ad5834fdde156e66ae6c9ff0b5622b73b751450f3ea791136cbd69f4",
        }

        for relative_path, expected_hash in expected_sha256.items():
            data = (ROOT / relative_path).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), expected_hash, relative_path)


if __name__ == "__main__":
    unittest.main()
