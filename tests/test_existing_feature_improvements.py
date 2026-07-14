import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ExistingFeatureImprovementTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_playlist_and_remote_source_hardening(self):
        source = self.read("src/network_sources.cpp")
        for token in (
            "UnquotePlaylistValue",
            "CURLE_LOGIN_DENIED",
            "authentication failed",
            "Remote directory listing empty",
            'trimmed.rfind("total ", 0)',
            "detailDirectory",
        ):
            self.assertIn(token, source)
        self.assertIn("ResolvePlaylistEntry(playlistPath", source)
        self.assertIn("ResolvePlaylistSidecar(playlistPath", source)

    def test_proxy_streams_controls_remote_didl_url(self):
        source = self.read("src/contentdirectory.cpp")
        self.assertIn("IsRemoteMediaUrl(it.path) && !proxyStreams", source)
        self.assertIn('"/media/" + std::to_string(it.id)', source)
        self.assertIn("XMLEscapeUtf8(WideToUtf8(it.path))", source)

    def test_config_schema_stays_stable(self):
        config = self.read("src/config.h") + self.read("src/config.cpp") + self.read("src/posix_config.cpp")
        for forbidden in ("ShowVideoThumbnails", "ShowImageThumbnails", "ShowAudioAlbumArt", "AutoWifi", "QrPage"):
            self.assertNotIn(forbidden, config)


if __name__ == "__main__":
    unittest.main()
