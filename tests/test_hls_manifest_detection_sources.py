import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class HlsManifestDetectionSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_hls_detection_helpers_are_declared_and_defined(self):
        header = self.read("src/network_sources.h")
        source = self.read("src/network_sources.cpp")

        self.assertIn("bool IsHlsManifestText(const std::string& text);", header)
        self.assertIn("FetchedPlaylist FetchPlaylistOnce(const std::wstring& playlistPath);", header)
        self.assertIn("std::vector<PlaylistEntry> ParseFetchedPlaylistText(const std::wstring& playlistPath, const std::string& text);", header)
        self.assertIn("bool IsHlsManifestText(const std::string& text) {", source)
        self.assertIn('trimmed.rfind("#EXT-X-", 0) == 0', source)
        self.assertIn("FetchedPlaylist FetchPlaylistOnce(const std::wstring& playlistPath) {", source)
        self.assertIn("ReadSourceText(playlistPath", source)

    def test_m3u8_still_excluded_from_generic_playable_kformats(self):
        import re
        utils = self.read("src/dlna_utils.cpp")
        match = re.search(r"const ExtensionFormat kFormats\[\] = \{(.*?)\};", utils, re.DOTALL)
        self.assertIsNotNone(match)
        self.assertNotIn('L".m3u8"', match.group(1))


if __name__ == "__main__":
    unittest.main()