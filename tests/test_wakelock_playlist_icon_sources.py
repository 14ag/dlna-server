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
            for token in ("DefaultPlaylistEnabled", "DefaultPlaylistPath", "ServerIconPath", "GetDefaultPlaylistPath"):
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

    def test_settings_generate_m3u_and_server_icon_controls(self):
        for path in ("src/settingsdlg.cpp", "src/fltk_gui_main.cpp"):
            source = self.read(path)
            for token in (
                "#EXTM3U",
                "#EXTINF:-1,",
                "#DLNA-SUBTITLE:",
                "#EXTVLCOPT:sub-file=",
                "Default playlist",
                "server icon",
            ):
                self.assertIn(token, source)

    def test_device_description_and_http_servers_expose_icon(self):
        content = self.read("src/contentdirectory.cpp")
        self.assertIn("<iconList>", content)
        self.assertIn("/server-icon", content)

        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(path)
            self.assertIn('path == "/server-icon"', source)
            self.assertIn("LoadServerIcon", source)
            self.assertIn("image/png", source)
            self.assertIn("image/jpeg", source)

        resources = self.read("resources/resource.h")
        app_rc = self.read("resources/app.rc")
        self.assertIn("IDR_APP_ICON_BYTES", resources)
        self.assertIn('IDR_APP_ICON_BYTES RCDATA "app.ico"', app_rc)


if __name__ == "__main__":
    unittest.main()
