import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class DlnaReviewFixSourceTests(unittest.TestCase):
    def test_media_scan_builds_indexes_and_swaps_state(self):
        header = read_text("src/media_sources.h")
        combined = read_text("src/media_sources.cpp") + read_text("src/posix_media_sources.cpp")

        for token in (
            "struct MediaIndexState",
            "std::atomic<int> m_systemUpdateId",
            "std::unordered_map<int, size_t> idToIndex",
            "std::unordered_map<int, std::vector<size_t>> childrenByParent",
            "std::unordered_set<std::wstring> duplicateKeys",
            "BuildIndexes",
            "SwapScannedState",
            "GetDescendants",
            "GetChildCount",
        ):
            self.assertIn(token, header + combined)

        self.assertNotIn("std::lock_guard<std::mutex> lock(m_mutex);\n    m_items.clear();", combined)

    def test_discovery_uses_nonblocking_delayed_responses_and_safe_stop(self):
        ssdp_h = read_text("src/ssdp.h")
        win = read_text("src/ssdp.cpp")
        posix = read_text("src/posix_ssdp.cpp")
        combined = ssdp_h + win + posix

        for token in (
            "QueueSearchResponses",
            "DelayedSearchResponse",
            "m_responseCondition",
            "ResponseWorker",
            "ComputeDelayMilliseconds",
            "GetDlnaServerHeader",
            "GetTickCount64",
        ):
            self.assertIn(token, combined)

        self.assertNotIn("Sleep(delayMs);\n    }\n\n    if (!SetOutboundInterface(socket", win)
        self.assertNotIn("CloseSockets();\n\n    if (m_hThread)", win)
        self.assertNotIn("CloseSockets();\n    if (m_thread.joinable())", posix)

    def test_http_remote_io_and_runtime_limits_are_hardened(self):
        cmake = read_text("CMakeLists.txt")
        network = read_text("src/network_sources.cpp")
        http = read_text("src/httpserver.cpp")
        posix_http = read_text("src/posix_httpserver.cpp")
        posix_log = read_text("src/posix_log.cpp")

        for token in (
            "find_package(CURL REQUIRED)",
            "DLNA_HAS_LIBCURL",
            "CURL::libcurl",
            "curl_easy_perform",
            "CurlGlobalInit",
            "curl_easy_setopt",
            "CURLOPT_RANGE",
            "curl_easy_getinfo",
            "Remote content unavailable",
            "ReadHttpRequestHeaders",
            "Connection: keep-alive",
            "kMaxClientThreads",
            "ReapFinishedClientThreads",
            "kMaxLogLines",
        ):
            self.assertIn(token, cmake + network + http + posix_http + posix_log)

        for forbidden in ("CreateProcessW", "execvp", "RunCurlWithReader"):
            self.assertNotIn(forbidden, network)

    def test_config_metadata_network_and_whitelist_fixes_exist(self):
        config_h = read_text("src/config.h")
        config = read_text("src/config.cpp") + read_text("src/posix_config.cpp")
        content = read_text("src/contentdirectory.cpp")
        net = read_text("src/netutils.cpp") + read_text("src/posix_netutils.cpp")
        whitelist = read_text("src/ipwhitelist.h") + read_text("src/ipwhitelist.cpp")
        posix_server = read_text("src/posix_server.cpp")
        posix_config = read_text("src/posix_config.cpp")

        for token in (
            "deviceManufacturer",
            "deviceModelName",
            "presentationUrl",
            "<presentationURL>",
            "IsIPv4Apipa",
            "AddEndpointForUnicast",
            "std::unordered_set<std::string> m_allowedIps",
            "struct CidrRange",
            "ParseCidrRange",
            "IsInRange",
            "std::random_device rd",
            "rd.entropy()",
            "No media sources configured",
        ):
            self.assertIn(token, config_h + config + content + net + whitelist + posix_server + posix_config)

        self.assertNotIn("AppConfig.mediaSources.push_back({L\".\", true});", posix_server)

    def test_small_hot_paths_and_docs_are_updated(self):
        utils = read_text("src/dlna_utils.cpp")
        content = read_text("src/contentdirectory.cpp")
        readme = read_text("README.md")
        hardening = read_text("docs/hardening-notes.md")

        for token in (
            "std::unordered_set<std::string> seen",
            "didl.reserve",
            "GetDescendants",
            "libcurl",
            "background media scan",
            "CIDR",
        ):
            self.assertIn(token, utils + content + readme + hardening)


if __name__ == "__main__":
    unittest.main()
