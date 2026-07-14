import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class HardeningFixSourceTests(unittest.TestCase):
    def read(self, path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")

    def test_whitelist_and_http_shutdown_are_synchronized(self):
        whitelist_h = self.read("src/ipwhitelist.h")
        whitelist_cpp = self.read("src/ipwhitelist.cpp")
        posix_http = self.read("src/posix_httpserver.cpp")
        http_h = self.read("src/httpserver.h")
        windows_http = self.read("src/httpserver.cpp")

        self.assertIn("std::shared_mutex", whitelist_h)
        self.assertIn("std::unique_lock<std::shared_mutex>", whitelist_cpp)
        self.assertIn("std::shared_lock<std::shared_mutex>", whitelist_cpp)
        self.assertIn("m_clientThreads", http_h + posix_http)
        self.assertNotIn(".detach()", posix_http)
        for source in (posix_http, windows_http):
            self.assertIn("SO_RCVTIMEO", source)
            self.assertIn("SO_SNDTIMEO", source)

    def test_posix_ssdp_has_ipv6_and_alive_refresh(self):
        source = self.read("src/posix_ssdp.cpp")

        for token in (
            "kSsdpMulticastIPv6",
            "IPV6_JOIN_GROUP",
            "IPV6_MULTICAST_IF",
            "m_ipv6Socket = CreateIPv6Socket",
            'SendNotifyRound("ssdp:alive")',
            "remoteAddr->sa_family",
        ):
            self.assertIn(token, source)

    def test_ports_backups_ci_and_docs_are_hardened(self):
        utils = self.read("src/dlna_utils.h") + self.read("src/dlna_utils.cpp")
        config = self.read("src/config.cpp") + self.read("src/posix_config.cpp")
        win_settings = self.read("src/settingsdlg.cpp")
        fltk = self.read("src/fltk_gui_main.cpp")
        cmake = self.read("CMakeLists.txt")
        ci = self.read(".github/workflows/ci.yml")
        docs = self.read("docs/hardening-notes.md")
        gitignore = self.read(".gitignore")

        self.assertIn("TryParsePortStrict", utils)
        self.assertIn("ParsePortOrDefault", config)
        self.assertIn("Invalid port", win_settings)
        self.assertIn("HTTP port must be between 1 and 65535", fltk)
        self.assertIn("*.bak", gitignore)
        self.assertFalse(list((ROOT / "src").glob("*.bak")))
        self.assertIn("dlna_enable_warnings", cmake)
        self.assertIn("python -m pytest -q", ci)
        self.assertIn("cmake --build", ci)
        self.assertIn("shell-based remote source fetching", docs)


if __name__ == "__main__":
    unittest.main()
