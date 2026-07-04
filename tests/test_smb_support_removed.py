import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

UI_AND_DOC_FILES_MUST_NOT_MENTION_SMB = (
    "src/mainwindow.cpp",
    "src/fltk_gui_main.cpp",
    "src/settingsdlg.cpp",
    "README.md",
)


class SmbSupportRemovedTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_ui_and_docs_no_longer_mention_smb(self):
        for relative_path in UI_AND_DOC_FILES_MUST_NOT_MENTION_SMB:
            text = self.read(relative_path).lower()
            self.assertNotIn("smb", text, f"'smb' still present in {relative_path}")

    def test_scheme_helpers_no_longer_accept_smb(self):
        source = self.read("src/network_sources.cpp")
        supported_start = source.index("bool IsSupportedScheme(")
        supported_end = source.index("\n}\n", supported_start)
        share_start = source.index("bool IsNetworkShareUrl(")
        share_end = source.index("\n}\n", share_start)

        self.assertNotIn("smb", source[supported_start:supported_end])
        self.assertNotIn("smb", source[share_start:share_end])

    def test_removed_smb_source_path_helper_exists_and_is_used(self):
        header = self.read("src/network_sources.h")
        source = self.read("src/network_sources.cpp")

        self.assertIn("bool IsRemovedSmbSourcePath(const std::wstring& value);", header)
        self.assertIn("bool IsRemovedSmbSourcePath(const std::wstring& value) {", source)

        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            scan_source = self.read(path)
            self.assertIn("IsRemovedSmbSourcePath(", scan_source)
            self.assertIn("[media:smb-removed]", scan_source)


if __name__ == "__main__":
    unittest.main()