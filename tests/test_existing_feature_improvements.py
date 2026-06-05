import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ExistingFeatureImprovementTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_media_toggles_are_applied_in_both_scanners(self):
        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(path)
            self.assertIn("AppConfig.flatFolderStyle", source)
            self.assertIn("AppConfig.sortByTitle", source)
            self.assertIn("AppConfig.showFileNamesInsteadOfTitles", source)
            self.assertIn("AppConfig.addArtistAlbumFolders", source)
            self.assertIn("AddArtistAlbumMirrorIfPresent", source)
            self.assertIn("FindOrAddContainer", source)
            self.assertIn("ScanFolder(state,", source)
            self.assertIn("ScanNetworkFolder(state,", source)

    def test_playlist_order_preserved_until_sort_requested(self):
        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(path)
            self.assertIn("if (AppConfig.sortByTitle)", source)
            self.assertIn("std::sort", source)
            self.assertIn("NaturalLessWide(a.title, b.title)", source)

        content = self.read("src/contentdirectory.cpp")
        self.assertIn("sortCriteria.empty() && AppConfig.sortByTitle", content)
        self.assertIn("SortItems(results, sortCriteria)", content)

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
        self.assertIn("IsRemoteMediaUrl(it.path) && !AppConfig.proxyStreams", source)
        self.assertIn('"/media/" + std::to_string(it.id)', source)
        self.assertIn("XMLEscapeUtf8(WideToUtf8(it.path))", source)

    def test_album_art_lookup_and_advertising_are_safer(self):
        for path in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(path)
            for token in ("thumb.jpg", "thumb.JPG", "thumb.jpeg", "thumb.JPEG", ".jpeg"):
                self.assertIn(token, source)

        content = self.read("src/contentdirectory.cpp")
        self.assertIn("IsRegularFileWide(it.albumArtPath)", content)
        self.assertIn("upnp:albumArtURI", content)

    def test_config_schema_stays_stable(self):
        config = self.read("src/config.h") + self.read("src/config.cpp") + self.read("src/posix_config.cpp")
        for forbidden in ("ShowVideoThumbnails", "ShowImageThumbnails", "ShowAudioAlbumArt", "AutoWifi", "QrPage"):
            self.assertNotIn(forbidden, config)


if __name__ == "__main__":
    unittest.main()
