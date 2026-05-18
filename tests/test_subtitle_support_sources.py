import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class SubtitleSupportSourceTests(unittest.TestCase):
    def read_source(self, name):
        return (SRC / name).read_text(encoding="utf-8")

    def test_media_item_stores_subtitle_path(self):
        header = self.read_source("media_sources.h")

        self.assertIn("std::wstring subtitlePath;", header)

    def test_scan_detects_companion_subtitle_files(self):
        source = self.read_source("media_sources.cpp")

        for extension in ('.srt', '.vtt', '.sub', '.ass', '.ssa'):
            self.assertIn(f'L"{extension}"', source)
        self.assertIn("GetFileAttributesW(candidate.c_str())", source)
        self.assertIn("fileInfo.subtitlePath = candidate;", source)

    def test_browse_xml_advertises_subtitle_url(self):
        source = self.read_source("contentdirectory.cpp")

        self.assertIn('xmlns:sec=\\"http://www.sec.co.kr/dlna\\"', source)
        self.assertIn("<sec:CaptionInfoEx", source)
        self.assertIn('"/subtitle/" << it.id', source)
        self.assertIn("!it.subtitlePath.empty()", source)

    def test_http_server_serves_subtitle_route(self):
        source = self.read_source("httpserver.cpp")

        self.assertIn("#include <shlwapi.h>", source)
        self.assertIn('path.rfind("/subtitle/", 0) == 0', source)
        self.assertIn("item.subtitlePath.c_str()", source)
        self.assertIn("text/srt; charset=utf-8", source)
        self.assertIn("done_subtitle:", source)


if __name__ == "__main__":
    unittest.main()
