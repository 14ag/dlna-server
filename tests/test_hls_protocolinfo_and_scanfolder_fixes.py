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
        self.assertIn("video/mpegurl", src)
        self.assertIn("BuildHlsProtocolInfo()", src)
        # The literal string should be centralized in dlna_utils.h
        src = self.read("src/dlna_utils.h")
        self.assertIn(
            "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000",
            src,
        )

    # ------------------------------------------------------------------
    # Fix 2: ScanFolder and ScanNetworkFolder do not pre-create containers
    # ------------------------------------------------------------------

    def _assert_scan_folder_hls_peek(self, path):
        src = self.read(path)
        # Must call ScanPlaylistTree for playlist files
        self.assertIn("ScanPlaylistTree", src)
        # Must call FetchPlaylistOnce in ScanOnePlaylistNode
        self.assertIn("FetchPlaylistOnce", src)
        # Must use ParseFetchedPlaylistText for non-HLS branch (no double-fetch)
        self.assertIn("ParseFetchedPlaylistText", src)
        # Must add HLS item in ScanOnePlaylistNode
        self.assertIn("AddHlsStreamItem(ctx->state, node.path, node.parentId, node.titleOverride)", src)

    def _assert_scan_network_hls_peek(self, path):
        src = self.read(path)
        self.assertIn("ScanPlaylistTree", src)
        self.assertIn("FetchPlaylistOnce", src)
        self.assertIn("AddHlsStreamItem(ctx->state, node.path, node.parentId, node.titleOverride)", src)


    # ------------------------------------------------------------------
    # Fix 3: HTTP servers emit android-matching contentFeatures for HLS
    # ------------------------------------------------------------------

    def _assert_http_hls_content_features(self, path):
        src = self.read(path)
        self.assertIn("video/mpegurl", src)
        # HLS content features are now emitted via BuildHlsContentFeatures()
        # in the early-return HLS branch, not via an inline ternary
        self.assertIn("BuildHlsContentFeatures()", src)

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
