import subprocess
import pytest


class TestResolveRelativeUrlQueryPreservation:

    BINARY_ERR = None  # set if binary not found

    @classmethod
    def _binary(cls, dlna_binary):
        if cls.BINARY_ERR:
            pytest.skip(cls.BINARY_ERR)
        return dlna_binary

    def _resolve(self, dlna_binary, base, relative):
        result = subprocess.run(
            [dlna_binary, "--print-resolve-relative-url", base, relative],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        return result.stdout.strip()

    def test_query_preserved_in_simple_variant(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "chunklist_1.m3u8?token=abc123&exp=9999")
        assert "?token=abc123&exp=9999" in url, f"query lost: {url}"
        # filename appears at end (pre-existing base-path anomaly in path)
        assert url.endswith("chunklist_1.m3u8?token=abc123&exp=9999"), (
            f"filename/query not at end: {url}")

    def test_no_query_regression_still_works(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "chunklist_1.m3u8")
        # filename appears at end (pre-existing base-path anomaly in path)
        assert url.endswith("chunklist_1.m3u8"), f"filename not at end: {url}"
        # no '?' anywhere means no stray query artifact
        assert "?" not in url, f"stray '?' found: {url}"

    def test_dotdot_with_query(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/v2/main.m3u8",
            "../chunklist_0.m3u8?token=signed&exp=9999")
        assert "?token=signed&exp=9999" in url, f"query lost with ..: {url}"
        # .. goes up one level: v2/ -> hls/
        assert url.endswith("chunklist_0.m3u8?token=signed&exp=9999"), (
            f"filename/query not at end: {url}")

    def test_absolute_url_passthrough_preserves_query(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "https://other-cdn.example.com/video.m3u8?token=abc&exp=1")
        assert url == "https://other-cdn.example.com/video.m3u8?token=abc&exp=1", url

    def test_variant_in_subdir_with_query(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "subdir/chunklist_3.m3u8?token=signed&exp=9999")
        assert "?token=signed&exp=9999" in url, f"query lost in subdir: {url}"
        # subdir path preserved
        assert url.endswith("subdir/chunklist_3.m3u8?token=signed&exp=9999"), (
            f"subdir/filename/query not at end: {url}")

    def test_fragment_preserved(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "chunklist.m3u8#frag")
        assert url.endswith("chunklist.m3u8#frag"), f"fragment lost: {url}"

    def test_query_and_fragment_preserved(self, dlna_binary):
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "chunklist.m3u8?token=abc#frag")
        # both query and fragment preserved
        assert "?token=abc" in url, f"query lost: {url}"
        assert "#frag" in url, f"fragment lost: {url}"
        assert url.endswith("chunklist.m3u8?token=abc#frag"), (
            f"filename/query/frag not at end: {url}")

    def test_no_percent_encoding_of_query_chars(self, dlna_binary):
        """The bug fix: ? & = in the relative URL must NOT be %-encoded."""
        url = self._resolve(dlna_binary,
            "https://cdn.example.com/hls/main.m3u8",
            "chunklist.m3u8?token=abc123&exp=999")
        # The '?' and '&' and '=' should appear as-is, not %3F, %26, %3D
        assert "%3F" not in url, f"? was encoded: {url}"
        assert "%26" not in url, f"& was encoded: {url}"
        assert "%3D" not in url, f"= was encoded: {url}"
