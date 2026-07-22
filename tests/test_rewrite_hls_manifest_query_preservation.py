import subprocess
from pathlib import Path

FIXTURE = (Path(__file__).resolve().parent /
           "fixtures" / "hls-master-with-signed-variants.m3u8")


class TestRewriteHlsManifestQueryPreservation:

    def _rewrite(self, dlna_binary, base_url):
        result = subprocess.run(
            [dlna_binary, "--print-rewrite-hls-manifest",
             base_url, str(FIXTURE)],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        return result.stdout

    def test_simple_variant_query_preserved(self, dlna_binary):
        out = self._rewrite(dlna_binary,
                            "https://cdn.example.com/hls/main.m3u8")
        lines = [l for l in out.splitlines()
                 if l.strip() and not l.strip().startswith("#")]
        # chunklist_1.m3u8?token=signed&exp=1999999999
        assert any("chunklist_1.m3u8?token=signed&exp=1999999999"
                   in l for l in lines), f"query lost in:\n{out}"

    def test_subdir_variant_path_correct(self, dlna_binary):
        out = self._rewrite(dlna_binary,
                            "https://cdn.example.com/hls/main.m3u8")
        lines = [l for l in out.splitlines()
                 if l.strip() and not l.strip().startswith("#")]
        # subdir/chunklist_3.m3u8?token=signed&exp=1999999999
        assert any(
            "subdir/chunklist_3.m3u8?token=signed&exp=1999999999"
            in l for l in lines), f"subdir resolve wrong:\n{out}"

    def test_dotdot_resolved_correctly_with_query(self, dlna_binary):
        out = self._rewrite(dlna_binary,
                            "https://cdn.example.com/hls/main.m3u8")
        lines = [l for l in out.splitlines()
                 if l.strip() and not l.strip().startswith("#")]
        # ../mobile/chunklist_0.m3u8?token=signed&exp=1999999999
        assert any(
            "mobile/chunklist_0.m3u8?token=signed&exp=1999999999"
            in l for l in lines), f".. resolve wrong:\n{out}"

    def test_tags_preserved(self, dlna_binary):
        out = self._rewrite(dlna_binary,
                            "https://cdn.example.com/hls/main.m3u8")
        assert "#EXTM3U" in out
        assert "#EXT-X-STREAM-INF" in out

    def test_all_variants_have_absolute_base(self, dlna_binary):
        out = self._rewrite(dlna_binary,
                            "https://cdn.example.com/hls/main.m3u8")
        for line in out.splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            assert stripped.startswith("https://cdn.example.com/"), (
                f"URI not absolute: {stripped}")
