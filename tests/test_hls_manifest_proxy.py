import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


# ---------------------------------------------------------------------------
# Source-contract tests (fast, no server needed)
# Verify Phase 3 HLS manifest URI rewrite design:
#   - isHlsManifest variable removed from both httpserver files
#   - HLS items handled by early return branch before remote/local paths
#   - FetchHlsManifestForServing + BuildHlsContentFeatures used in HLS branch
#   - Remote/local branches no longer have HLS ternaries
#   - Samsung spoof still present and unchanged
# ---------------------------------------------------------------------------

class HlsManifestProxyFixSourceTests(unittest.TestCase):
    def _read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    # --- Task 1: Windows httpserver checks ---

    def test_windows_hls_early_return_branch(self):
        src = self._read("src/httpserver.cpp")
        # HLS items are handled before IsRemoteMediaUrl check
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        idx_remote = src.find("if (IsRemoteMediaUrl(item.path))")
        self.assertGreater(idx_hls, 0, "HLS mime check not found")
        self.assertGreater(idx_remote, 0, "IsRemoteMediaUrl not found")
        self.assertLess(idx_hls, idx_remote,
                        "HLS branch must appear before IsRemoteMediaUrl")

    def test_windows_isHlsManifest_removed(self):
        src = self._read("src/httpserver.cpp")
        self.assertNotIn("isHlsManifest", src,
                         "isHlsManifest must not exist in httpserver.cpp")

    def test_windows_hls_branch_uses_fetch_and_features(self):
        src = self._read("src/httpserver.cpp")
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        self.assertGreater(idx_hls, 0)
        region = src[idx_hls:idx_hls + 1500]
        self.assertIn("HlsManifestFetchResult", region)
        self.assertIn("FetchHlsManifestForServing", region)
        self.assertIn("BuildHlsContentFeatures()", region)
        self.assertIn("<< manifest.text.size()", region)
        self.assertIn('Accept-Ranges: none', region)

    def test_windows_non_hls_accept_ranges_bytes_preserved(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn('Accept-Ranges: bytes', src)

    def test_windows_spoofSamsung_unchanged(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn("Content-Length: 1073741824", src)
        self.assertIn("Accept-Ranges: none", src)

    # --- Task 2: POSIX httpserver checks ---

    def test_posix_hls_early_return_branch(self):
        src = self._read("src/posix_httpserver.cpp")
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        idx_remote = src.find("if (IsRemoteMediaUrl(item.path))")
        self.assertGreater(idx_hls, 0, "HLS mime check not found")
        self.assertGreater(idx_remote, 0, "IsRemoteMediaUrl not found")
        self.assertLess(idx_hls, idx_remote,
                        "HLS branch must appear before IsRemoteMediaUrl")

    def test_posix_isHlsManifest_removed(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertNotIn("isHlsManifest", src,
                         "isHlsManifest must not exist in posix_httpserver.cpp")

    def test_posix_hls_branch_uses_fetch_and_features(self):
        src = self._read("src/posix_httpserver.cpp")
        idx_hls = src.find('item.mimeType == L"video/mpegurl"')
        self.assertGreater(idx_hls, 0)
        region = src[idx_hls:idx_hls + 1500]
        self.assertIn("HlsManifestFetchResult", region)
        self.assertIn("FetchHlsManifestForServing", region)
        self.assertIn("BuildHlsContentFeatures()", region)
        self.assertIn("<< manifest.text.size()", region)
        self.assertIn('Accept-Ranges: none', region)

    def test_posix_non_hls_accept_ranges_bytes_preserved(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn('Accept-Ranges: bytes', src)

    def test_posix_spoofSamsung_unchanged(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn("Content-Length: 1073741824", src)
        self.assertIn("Accept-Ranges: none", src)

    # --- Symmetry between both files ---

    def test_both_platforms_use_hls_fetch(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertIn("HlsManifestFetchResult", src)
            self.assertIn("FetchHlsManifestForServing", src)
            self.assertIn('L"video/mpegurl"', src)

    def test_neither_platform_has_ishlsmanifest(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertNotIn("isHlsManifest", src,
                             "isHlsManifest must not exist")

    def test_both_platforms_spoof_value_unchanged(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertIn("Content-Length: 1073741824", src)
            self.assertIn("Accept-Ranges: none", src)


if __name__ == "__main__":
    unittest.main()