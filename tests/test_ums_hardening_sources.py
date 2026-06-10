import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class UmsHardeningSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_shared_utility_layer_owns_common_protocol_rules(self):
        header = self.read("src/dlna_utils.h")
        source = self.read("src/dlna_utils.cpp")

        for symbol in (
            "FindHeaderValueCaseInsensitive",
            "ParseHttpRangeHeader",
            "GetMediaFormatForExtension",
            "BuildProtocolInfo",
            "BuildContentFeatures",
            "BuildSourceProtocolInfoList",
            "SubtitleMimeForExtension",
            "NaturalLessWide",
        ):
            self.assertIn(symbol, header)
            self.assertIn(symbol, source)
        self.assertIn('L".webm"', source)
        self.assertIn('L".m2ts"', source)
        self.assertIn('L".opus"', source)
        self.assertIn('L".dts"', source)
        self.assertIn("DLNA.ORG_PN", source)

    def test_content_directory_exposes_capabilities_and_faults(self):
        source = self.read("src/contentdirectory.cpp")
        header = self.read("src/contentdirectory.h")

        self.assertIn("GetSearchCapabilities", source)
        self.assertIn("GetSortCapabilities", source)
        self.assertIn("<action><name>Search</name>", source)
        self.assertIn("MatchesSearchCriteria", source)
        self.assertIn("BrowseSearchResponse", source)
        self.assertIn("BuildDIDL", source)
        self.assertIn("upnp:albumArtURI", source)
        self.assertIn("/albumart/", source)
        self.assertIn("SoapFault(401", source)
        self.assertIn("SoapFault(402", source)
        self.assertIn("SoapFault(701", source)
        self.assertIn("TryParseIntStrict", source)
        self.assertIn("NaturalLessWide", source)
        self.assertIn("HandleContentDirectoryControl", header + source)
        self.assertIn("HandleConnectionManagerControl", header + source)
        self.assertIn("BuildSourceProtocolInfoList", source)
        self.assertIn("<Source>", source)
        self.assertIn("<Sink></Sink>", source)

    def test_windows_and_posix_http_use_shared_range_and_head_logic(self):
        for name in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(name)
            self.assertIn('method == "GET" || method == "HEAD"', source)
            self.assertIn("ParseHttpRangeHeader", source)
            self.assertIn("TryParseIntStrict", source)
            self.assertIn("Content-Length", source)
            self.assertIn("contentLength < 0", source)
            self.assertIn("416 Range Not Satisfiable", source)
            self.assertIn("Content-Range: bytes */", source)
            self.assertIn("SubtitleMimeForExtension", source)
            self.assertIn("BuildContentFeaturesForExtension", source)
            self.assertIn('path.rfind("/albumart/", 0) == 0', source)
            self.assertIn("albumArtPath", source)
            self.assertIn("/upnp/control/connection_manager", source)
            self.assertIn("HandleConnectionManagerControl", source)
            self.assertIn('method == "SUBSCRIBE" || method == "UNSUBSCRIBE"', source)
            self.assertIn("AppEvents.HandleEventSubscription", source)
            self.assertNotIn("EventSubscriptionResponse", source)

        eventing = self.read("src/upnp_eventing.cpp")
        self.assertIn("/upnp/event/content_directory", eventing)
        self.assertIn("/upnp/event/connection_manager", eventing)
        self.assertIn("412 Precondition Failed", eventing)
        utils = self.read("src/dlna_utils.cpp")
        self.assertIn("fileSize <= 0", utils)

    def test_scanners_share_mime_table_and_subtitle_detection(self):
        for name in ("src/media_sources.cpp", "src/posix_media_sources.cpp"):
            source = self.read(name)
            self.assertIn("GetMediaFormatForExtension", source)
            self.assertIn("NaturalLessWide", source)
            self.assertIn("BuildDuplicateMediaKey", source)
            self.assertIn("duplicateKeys", source)
            self.assertIn("SetAlbumArtIfExists", source)
            self.assertIn("albumArtPath", source)
            self.assertIn('L".smi"', source) if name.endswith("media_sources.cpp") and not name.endswith("posix_media_sources.cpp") else self.assertIn('".smi"', source)

        header = self.read("src/media_sources.h")
        self.assertIn("albumArtPath", header)
        self.assertIn("albumArtMime", header)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT", self.read("src/media_sources.cpp"))
        posix_source = self.read("src/posix_media_sources.cpp")
        self.assertIn("entry.is_symlink", posix_source)
        self.assertIn("IsHiddenPath", posix_source)
        self.assertIn("IsReadableEntry", posix_source)


if __name__ == "__main__":
    unittest.main()
