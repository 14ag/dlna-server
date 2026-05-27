import unittest
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
            "EnableWindow(m_hBtnStartStop, enabled)",
        ):
            self.assertIn(token, header + source)

        self.assertNotIn("ES_AWAYMODE_REQUIRED", source)
        self.assertNotIn("m_isRunning", header + source)

    def test_default_playlist_config_and_parser_include_subtitles(self):
        for path in ("src/config.cpp", "src/posix_config.cpp"):
            source = self.read(path)
            for token in ("DefaultPlaylistEnabled", "DefaultPlaylistPath", "GetDefaultPlaylistPath"):
                self.assertIn(token, source)

        network_header = self.read("src/network_sources.h")
        network_source = self.read("src/network_sources.cpp")
        self.assertIn("std::wstring subtitlePath;", network_header)
        self.assertIn("#DLNA-SUBTITLE:", network_source)
        self.assertIn("#EXTVLCOPT:sub-file=", network_source)
        self.assertIn("pendingSubtitle", network_source)

        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(path)
            self.assertIn("AppConfig.defaultPlaylistEnabled", source)
            self.assertIn("Default playlist", source)
            self.assertIn("entry.subtitlePath", source)

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

        resources = self.read("resources/resource.h")
        app_rc = self.read("resources/app.rc")
        for size in ("48", "120", "256"):
            self.assertIn(f"IDR_SERVER_ICON_{size}", resources)
            self.assertIn(f'IDR_SERVER_ICON_{size} RCDATA "server_icon_{size}.png"', app_rc)


if __name__ == "__main__":
    unittest.main()
