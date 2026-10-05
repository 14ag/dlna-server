import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class HlsProtocolInfoAndScanFolderFixTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

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


if __name__ == "__main__":
    unittest.main()
