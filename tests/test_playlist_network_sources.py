import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PlaylistNetworkSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_shared_network_helper_supports_playlists_and_remote_shares(self):
        header = self.read("src/network_sources.h")
        source = self.read("src/network_sources.cpp")

        for token in (
            "LoadPlaylistEntries",
            "ListRemoteDirectory",
            "ProbeRemoteContentLength",
            "StreamRemoteContent",
            "IsNetworkShareUrl",
        ):
            self.assertIn(token, header)
            self.assertIn(token, source)

        for token in ('L".m3u"', 'L".m3u8"', 'L".pls"', '"ftp"', "CURLOPT_DIRLISTONLY", "CURLOPT_RANGE"):
            self.assertIn(token, source)

    def test_windows_and_posix_scanners_index_playlist_and_network_sources(self):
        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(path)
            self.assertIn("IsPlaylistSourcePath", source)
            self.assertIn("ScanPlaylist", source)
            self.assertIn("IsNetworkShareUrl", source)
            self.assertIn("ScanNetworkFolder", source)
            # scanners now use FetchPlaylistOnce/ParseFetchedPlaylistText instead of LoadPlaylistEntries directly
            self.assertIn("ScanPlaylistEntry", source)
            self.assertIn("ParseFetchedPlaylistText", source)
            self.assertIn("ListRemoteDirectory", source)
            self.assertIn("ProbeRemoteContentLength", source)

    def test_http_servers_proxy_remote_media_with_range_support(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(path)
            self.assertIn("IsRemoteMediaUrl", source)
            self.assertIn("StreamRemoteContent", source)
            self.assertIn("ProbeRemoteContentLength", source)
            self.assertIn("Accept-Ranges: none", source)
            self.assertIn("BuildContentFeaturesForExtension", source)

    def test_gui_paths_accept_playlists_and_network_urls(self):
        windows_gui = self.read("src/mainwindow.cpp")
        fltk_gui = self.read("src/fltk_gui_main.cpp")

        for token in ("Playlist", "Network", "ftp://user:pass@server"):
            self.assertIn(token, windows_gui + fltk_gui)
        self.assertNotIn("smb://", windows_gui + fltk_gui)

        self.assertIn("BrowsePlaylist", windows_gui)
        self.assertIn("Fl_Native_File_Chooser", fltk_gui)
        self.assertIn("fl_input", fltk_gui)


if __name__ == "__main__":
    unittest.main()
