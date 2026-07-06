import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class HlsProtocolInfoAndScanFolderFixTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    # ------------------------------------------------------------------
    # Fix 1: ContentDirectory Browse emits android-matching protocolInfo
    # ------------------------------------------------------------------

    def test_hls_protocolinfo_uses_android_op01_flags(self):
        src = self.read("src/contentdirectory.cpp")
        # ItemProtocolInfo must detect HLS MIME and emit OP=01 (time-seek)
        # matching the android j.java contentFeatures.dlna.org pattern
        self.assertIn("application/vnd.apple.mpegurl", src)
        self.assertIn(
            "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000",
            src,
        )

    def test_hls_mime_guard_present_in_contentdirectory(self):
        src = self.read("src/contentdirectory.cpp")
        self.assertIn("ItemProtocolInfo", src)
        self.assertIn("application/vnd.apple.mpegurl", src)

    # ------------------------------------------------------------------
    # Fix 2: ScanFolder and ScanNetworkFolder do not pre-create containers
    # ------------------------------------------------------------------

    def _assert_scan_folder_hls_peek(self, path):
        src = self.read(path)
        # Must call FetchPlaylistOnce before pushing container
        self.assertIn("FetchPlaylistOnce", src)
        # Must use ParseFetchedPlaylistText for non-HLS branch (no double-fetch)
        self.assertIn("ParseFetchedPlaylistText", src)
        # Must add HLS item directly under parentId with no container
        self.assertIn("AddHlsStreamItem(state, fullPath, parentId)", src)

    def _assert_scan_network_hls_peek(self, path):
        src = self.read(path)
        self.assertIn("FetchPlaylistOnce", src)
        self.assertIn("AddHlsStreamItem(state, entry.url, parentId", src)

    def test_win32_scan_folder_hls_peek(self):
        self._assert_scan_folder_hls_peek("src/media_sources.cpp")

    def test_win32_scan_network_folder_hls_peek(self):
        self._assert_scan_network_hls_peek("src/media_sources.cpp")

    def test_posix_scan_folder_hls_peek(self):
        self._assert_scan_folder_hls_peek("src/posix_media_sources.cpp")

    def test_posix_scan_network_folder_hls_peek(self):
        self._assert_scan_network_hls_peek("src/posix_media_sources.cpp")

    # ------------------------------------------------------------------
    # Fix 3: HTTP servers emit android-matching contentFeatures for HLS
    # ------------------------------------------------------------------

    def _assert_http_hls_content_features(self, path):
        src = self.read(path)
        self.assertIn("application/vnd.apple.mpegurl", src)
        self.assertIn(
            "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000",
            src,
        )

    def test_win32_httpserver_hls_content_features(self):
        self._assert_http_hls_content_features("src/httpserver.cpp")

    def test_posix_httpserver_hls_content_features(self):
        self._assert_http_hls_content_features("src/posix_httpserver.cpp")

    # ------------------------------------------------------------------
    # Regression guard: .m3u8 must stay excluded from kFormats
    # ------------------------------------------------------------------

    def test_m3u8_still_excluded_from_generic_kformats(self):
        utils = self.read("src/dlna_utils.cpp")
        self.assertNotIn('L".m3u8"', utils)


if __name__ == "__main__":
    unittest.main()
