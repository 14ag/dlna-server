import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


# ---------------------------------------------------------------------------
# Source-contract tests (fast, no server needed)
# Verify the code changes from the HLS playlist blank-body fix workflow
# are present in both httpserver sources.
# ---------------------------------------------------------------------------

class HlsManifestProxyFixSourceTests(unittest.TestCase):
    def _read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    # --- Task 1: Windows httpserver checks ---

    def test_windows_ishlsmanifest_declared_before_range_parse(self):
        src = self._read("src/httpserver.cpp")
        idx_isHls = src.find("isHlsManifest")
        idx_guard = src.find("if (hasKnownSize && !isHlsManifest)")
        self.assertGreater(idx_isHls, 0, "isHlsManifest not found")
        self.assertGreater(idx_guard, 0, "Range guard if not found")
        self.assertLess(idx_isHls, idx_guard,
                        "isHlsManifest must appear before Range-guard if")

    def test_windows_range_parse_gated_by_ishlsmanifest(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn("if (hasKnownSize && !isHlsManifest)", src)
        self.assertIn("bool isPartial = hasKnownSize && !isHlsManifest && parsedRange.requested", src)
        self.assertIn("isHlsManifest ? fileSize : (endByte - startByte + 1)", src)

    def test_windows_hls_accept_ranges_none(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn('Accept-Ranges: " << (isHlsManifest ? "none" : "bytes")', src)

    def test_windows_spoofSamsung_unchanged(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn("Content-Length: 1073741824", src)
        # Find the hasKnownSize block, verify isHlsManifest does not appear
        # in the !hasKnownSize spoof region
        idx_spoof = src.find("Content-Length: 1073741824")
        self.assertGreater(idx_spoof, 0)
        # Check that isHlsManifest is NOT between the spoof line and the
        # preceding !hasKnownSize test
        before_spoof = src.rfind("!hasKnownSize", 0, idx_spoof)
        self.assertGreater(before_spoof, 0)
        region = src[before_spoof:idx_spoof + 50]
        # isHlsManifest may appear earlier but not in !hasKnownSize branch
        idx_hls_in_region = region.find("isHlsManifest")
        if idx_hls_in_region >= 0:
            # It might be the declaration at top of block, which is fine
            # Check if it appears in the spoof condition
            self.assertNotIn("isHlsManifest && !hasKnownSize", src)

    def test_windows_non_hls_accept_ranges_bytes_preserved(self):
        src = self._read("src/httpserver.cpp")
        self.assertIn(': "bytes"', src)  # non-HLS path still outputs bytes

    # --- Task 2: POSIX httpserver checks ---

    def test_posix_ishlsmanifest_declared_before_range_parse(self):
        src = self._read("src/posix_httpserver.cpp")
        idx_isHls = src.find("isHlsManifest")
        idx_guard = src.find("if (hasKnownSize && !isHlsManifest)")
        self.assertGreater(idx_isHls, 0, "isHlsManifest not found")
        self.assertGreater(idx_guard, 0, "Range guard if not found")
        self.assertLess(idx_isHls, idx_guard,
                        "isHlsManifest must appear before Range-guard if")

    def test_posix_range_parse_gated_by_ishlsmanifest(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn("if (hasKnownSize && !isHlsManifest)", src)
        self.assertIn("bool partial = hasKnownSize && !isHlsManifest && parsedRange.requested", src)
        self.assertIn("isHlsManifest ? fileSize : (end - start + 1)", src)

    def test_posix_hls_accept_ranges_none(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn('Accept-Ranges: " << (isHlsManifest ? "none" : "bytes")', src)

    def test_posix_bodyLength_variable_exists(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn("bodyLength", src)
        self.assertIn("long long bodyLength = hasKnownSize ? (isHlsManifest ? fileSize : (end - start + 1)) : 0", src)

    def test_posix_spoofSamsung_unchanged(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn("Content-Length: 1073741824", src)
        self.assertNotIn("isHlsManifest && !hasKnownSize", src)

    def test_posix_non_hls_accept_ranges_bytes_preserved(self):
        src = self._read("src/posix_httpserver.cpp")
        self.assertIn(': "bytes"', src)

    # --- Symmetry between both files ---

    def test_both_platforms_contain_ishlsmanifest(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertIn("const bool isHlsManifest", src)
            self.assertIn('L"application/vnd.apple.mpegurl"', src)

    def test_both_platforms_spoof_value_unchanged(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            src = self._read(path)
            self.assertIn("Content-Length: 1073741824", src)
            self.assertIn("Accept-Ranges: none", src)


if __name__ == "__main__":
    unittest.main()