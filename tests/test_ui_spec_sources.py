import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UiSpecTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_ui_spec_covers_cross_platform_contract(self):
        spec = self.read("docs/ui-spec.md")

        for text in (
            "Windows Win32 UI and Linux FLTK UI",
            "Minimum content size: 640 x 460",
            "Toolbar height: 48 px",
            "Status strip height: 24 px",
            "Add media folder",
            "Start server",
            "Stop server",
            "Settings",
            "Thumbnail controls remain visible but disabled",
            "Log text is read-only",
            "No clipped labels",
        ):
            self.assertIn(text, spec)

    def test_release_version_is_1_2_0(self):
        cmake = self.read("CMakeLists.txt")
        changelog = self.read("CHANGELOG.md")

        self.assertIn("project(dlna-server VERSION 1.2.0)", cmake)
        self.assertIn("## [1.2.0] - 2026-05-21", changelog)


if __name__ == "__main__":
    unittest.main()
