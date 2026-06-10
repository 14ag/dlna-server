import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent


def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def get_source_bundle(*paths: str) -> str:
    return "\n".join(read_source(path) for path in paths)


def get_review_blueprint_text() -> str:
    return (WORKSPACE / "misc" / "patches" / "dlna-server-review-6-6-26-implementation-blueprint.md").read_text(encoding="utf-8")


class ReviewSixJuneSourceContracts(unittest.TestCase):
    def test_blueprint_written_to_patches_folder(self):
        blueprint = get_review_blueprint_text()

        self.assertIn("DLNA Server Review 2026-06-06 Implementation Blueprint", blueprint)
        self.assertIn("Plan covers findings 1-60", blueprint)
        self.assertIn("Runtime blackbox tests are required", blueprint)

    def test_shared_helpers_replace_dead_and_duplicate_surfaces(self):
        utils = get_source_bundle("src/dlna_utils.h", "src/dlna_utils.cpp")
        config = read_source("src/config.cpp")
        netutils = read_source("src/netutils.cpp")
        content_header = read_source("src/contentdirectory.h")

        self.assertIn("std::wstring ToLowerWide(std::wstring value)", utils)
        self.assertEqual(config.count("std::string TrimAscii("), 0)
        self.assertEqual(netutils.count("std::string TrimAscii("), 0)
        self.assertEqual(netutils.count("std::string ToLowerAscii("), 0)
        self.assertNotIn("HandleBrowse", content_header)
        self.assertNotIn("GetMimeType", content_header)
        self.assertNotIn("GetUPnPClass", content_header)

    def test_soap_dispatch_uses_exact_action_names_and_bounded_post_bodies(self):
        content = read_source("src/contentdirectory.cpp")
        http = get_source_bundle("src/httpserver.cpp", "src/posix_httpserver.cpp")

        self.assertIn("ExtractSoapActionName", content)
        self.assertIn('action != "Browse"', content)
        self.assertIn('action == "Search"', content)
        self.assertIn('action == "GetProtocolInfo"', content)
        self.assertIn("SoapFault(400, \"Bad Request\")", content)
        self.assertNotIn("IsValidXml", content)
        self.assertNotIn('req.find("Browse")', content)
        self.assertNotIn('req.find("Search")', content)
        self.assertNotIn('req.find("GetProtocolInfo")', content)
        self.assertIn("kMaxSoapBodyBytes", http)
        self.assertIn("TryParseNonNegativeLongLong", http)
        self.assertNotIn("int contentLength = 0", http)

    def test_ssdp_uses_strict_mx_parse_random_delay_and_safe_waits(self):
        ssdp = get_source_bundle("src/ssdp.h", "src/ssdp.cpp", "src/posix_ssdp.cpp")

        self.assertIn("m_socketMutex", ssdp)
        self.assertIn("std::random_device", ssdp)
        self.assertIn("std::uniform_int_distribution", ssdp)
        self.assertIn("TryParseIntStrict(TrimAscii(mx", ssdp)
        self.assertIn("wait_until(lock, next->dueAt)", ssdp)
        self.assertIn("WaitForSingleObject(m_hThread, INFINITE)", ssdp)
        self.assertNotIn("atoi(", ssdp)
        self.assertNotIn("wait_for(lock, std::chrono::milliseconds(50))", ssdp)
        self.assertNotIn("WaitForSingleObject(m_hThread, 2000)", ssdp)

    def test_media_scan_and_didl_hot_paths_are_cached_or_batched(self):
        media = get_source_bundle("src/media_sources.h", "src/media_sources.cpp", "src/posix_media_sources.cpp")
        content = read_source("src/contentdirectory.cpp")
        utils = get_source_bundle("src/dlna_utils.h", "src/dlna_utils.cpp")

        self.assertIn("containerKeys", media)
        self.assertIn("albumArtByDirectory", media)
        self.assertIn("BuildAlbumArtCandidateNames", utils + media)
        self.assertIn("GetChildCounts", media + content)
        self.assertIn("AppendDescendants", media)
        self.assertIn("depth > 64", media)
        self.assertIn("mediaItemCount", media)
        self.assertNotIn("std::function<void(int)>", media)
        self.assertNotIn("IsRegularFileWide(it.albumArtPath)", content)
        self.assertIn("ConfigSnapshot", media + content)
        self.assertIn("AppConfig.Snapshot()", media + content)

    def test_remote_sources_and_logging_use_bounded_maintainable_paths(self):
        network = read_source("src/network_sources.cpp")
        logs = get_source_bundle("src/log.cpp", "src/posix_log.cpp")
        posix_http = read_source("src/posix_httpserver.cpp")
        posix_config = read_source("src/posix_config.cpp")

        self.assertIn("kCurlCaptureTimeoutSeconds", network)
        self.assertIn("CURLOPT_TIMEOUT, timeoutSeconds", network)
        self.assertIn("const std::function<bool", network)
        self.assertIn("std::vector<PlsEntryBuilder>", network)
        self.assertIn("detailDirectory || slashDirectory", network)
        self.assertNotIn("std::map<int", network)
        self.assertNotIn("auto callback = writeChunk", network)
        self.assertIn('L"a,ccs=UTF-8"', logs)
        self.assertIn("static FILE* g_debugLogFile", logs)
        self.assertIn("std::deque<std::wstring> g_lines", logs)
        self.assertIn("TimestampPrefix", logs)
        self.assertIn("ExecutableDirectory", posix_http)
        self.assertNotIn("GetDefaultPlaylistPath()", posix_http)
        self.assertIn("std::seed_seq seed", posix_config)
        self.assertNotIn("goto", get_source_bundle("src/httpserver.cpp"))
        self.assertIn("ScopedHandle", get_source_bundle("src/httpserver.cpp"))


if __name__ == "__main__":
    unittest.main()
