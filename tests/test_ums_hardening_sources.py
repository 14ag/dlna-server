import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class UmsHardeningSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_roadmap_groups_upgrade_work_by_dependency_depth(self):
        roadmap = self.read("docs/ums-upgrade-roadmap.md")

        for heading in (
            "No New Dependencies",
            "Optional FFmpeg/ffprobe",
            "Index/Database",
            "Renderer Profiles",
            "UPnP Advanced",
            "Later New Features",
        ):
            self.assertIn(f"## {heading}", roadmap)
        self.assertIn("clean-room", roadmap)
        self.assertIn("GPL implementation code must not be copied", roadmap)

    def test_shared_utility_layer_owns_common_protocol_rules(self):
        header = self.read("src/dlna_utils.h")
        source = self.read("src/dlna_utils.cpp")

        for symbol in (
            "FindHeaderValueCaseInsensitive",
            "ParseHttpRangeHeader",
            "GetMediaFormatForExtension",
            "SubtitleMimeForExtension",
            "NaturalLessWide",
        ):
            self.assertIn(symbol, header)
            self.assertIn(symbol, source)
        self.assertIn('L".webm"', source)
        self.assertIn('L".m2ts"', source)
        self.assertIn('L".opus"', source)

    def test_content_directory_exposes_capabilities_and_faults(self):
        source = self.read("src/contentdirectory.cpp")

        self.assertIn("GetSearchCapabilities", source)
        self.assertIn("GetSortCapabilities", source)
        self.assertIn("SoapFault(402", source)
        self.assertIn("SoapFault(701", source)
        self.assertIn("TryParseIntStrict", source)
        self.assertIn("NaturalLessWide", source)

    def test_windows_and_posix_http_use_shared_range_and_head_logic(self):
        for name in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(name)
            self.assertIn('method == "GET" || method == "HEAD"', source)
            self.assertIn("ParseHttpRangeHeader", source)
            self.assertIn("TryParseIntStrict", source)
            self.assertIn("416 Range Not Satisfiable", source)
            self.assertIn("Content-Range: bytes */", source)
            self.assertIn("SubtitleMimeForExtension", source)

    def test_scanners_share_mime_table_and_subtitle_detection(self):
        for name in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(name)
            self.assertIn("GetMediaFormatForExtension", source)
            self.assertIn("NaturalLessWide", source)
            self.assertIn('L".smi"', source) if name.endswith("media_sources.cpp") and not name.endswith("posix_media_sources.cpp") else self.assertIn('".smi"', source)

        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", self.read("src/media_sources.cpp"))
        self.assertIn("entry.is_symlink", self.read("src/posix_media_sources.cpp"))


if __name__ == "__main__":
    unittest.main()
