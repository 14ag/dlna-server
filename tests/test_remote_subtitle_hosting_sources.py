import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RemoteSubtitleHostingSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_posix_httpserver_handle_client_signature_is_not_duplicated(self):
        source = self.read("src/posix_httpserver.cpp")
        self.assertEqual(
            source.count("void HttpServer::HandleClient(int clientSocket, const std::string& clientIp) {"),
            1,
        )

    def test_subtitle_route_branches_on_remote_url_on_both_platforms(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(path)
            self.assertIn("IsRemoteMediaUrl(item.subtitlePath)", source)
            self.assertIn("StreamRemoteContent(item.subtitlePath", source)
            self.assertIn("ProbeRemoteContentLength(item.subtitlePath)", source)
            self.assertIn("SourceExtension(item.subtitlePath)", source)
            self.assertIn("RedactUrlForLog(item.subtitlePath)", source)
            self.assertIn("Remote subtitle unavailable", source)

    def test_subtitle_mime_lookup_no_longer_uses_raw_extension_apis(self):
        windows_http = self.read("src/httpserver.cpp")
        posix_http = self.read("src/posix_httpserver.cpp")
        self.assertNotIn("PathFindExtensionW(item.subtitlePath.c_str())", windows_http)
        self.assertNotIn("pathText.find_last_of('.')", posix_http)

    def test_didl_caption_type_strips_query_strings_via_source_extension(self):
        content = self.read("src/contentdirectory.cpp")
        self.assertIn("SourceExtension(it.subtitlePath)", content)
        self.assertNotIn("it.subtitlePath.rfind(L'.')", content)

    def test_remote_subtitle_playlist_round_trip_helpers_exist(self):
        network = self.read("src/network_sources.cpp")
        for token in ("ResolvePlaylistSidecar", "#DLNA-SUBTITLE:", "#EXTVLCOPT:sub-file="):
            self.assertIn(token, network)

    def test_scan_time_remote_subtitle_logging_present(self):
        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(path)
            self.assertIn("Playlist subtitle resolved to remote URL", source)


if __name__ == "__main__":
    unittest.main()