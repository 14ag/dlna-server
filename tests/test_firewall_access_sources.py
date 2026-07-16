import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FirewallAccessSourceTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_windows_firewall_helper_uses_app_scoped_inetfwpolicy2_rules(self):
        source = self.read("src/firewall_access_win.cpp")
        cmake = self.read("CMakeLists.txt")
        main = self.read("src/main.cpp")
        server = self.read("src/server.cpp")

        for token in (
            "INetFwPolicy2",
            "NetFwRule",
            "put_ApplicationName",
            "LocalSubnet",
            "NET_FW_PROFILE2_DOMAIN",
            "NET_FW_PROFILE2_PRIVATE",
            "NET_FW_PROFILE2_PUBLIC",
            "put_EdgeTraversal(VARIANT_FALSE)",
            "ShellExecuteExW",
            'L"runas"',
            'L"--configure-firewall --port "',
            "BuildTcpRuleName",
            "BuildUdpRuleName",
            "DLNA Server-",
            "HTTP TCP",
            "SSDP UDP",
        ):
            self.assertIn(token, source)

        for token in (
            "RuleAllowsAnyLocalPort",
            "RuleIsCompleteTcpAllow",
            "RuleIsCompleteUdpAllow",
            "RuleIsOldPortScopedTcpAllow",
            "get_Grouping",
            "NET_FW_IP_PROTOCOL_ANY",
            "MessageIndicatesAccessDenied",
            "AddRule(rules, BuildTcpRuleName(exePath)",
            "AddRule(rules, BuildUdpRuleName(exePath)",
            "put_LocalPorts",
        ):
            self.assertIn(token, source)

        self.assertIn("firewall_access_win.cpp", cmake)
        self.assertIn("--configure-firewall", main)
        self.assertIn("ConfigureFirewallAccessElevated", main)
        self.assertIn("EnsureFirewallAccess(cfg.port, FirewallAccessMode::Interactive", server)

    def test_posix_build_has_no_firewall_helper(self):
        cmake = self.read("CMakeLists.txt")
        cli = self.read("src/posix_main.cpp")
        gui = self.read("src/fltk_gui_main.cpp")

        self.assertFalse((ROOT / "src/firewall_access_posix.cpp").exists())
        self.assertNotIn("firewall_access_posix.cpp", cmake)
        self.assertNotIn("firewall_access.h", cli)
        self.assertNotIn("EnsureFirewallAccess", cli)
        self.assertNotIn("firewall_access.h", gui)
        self.assertNotIn("EnsureFirewallAccess", gui)

    def test_http_debug_log_records_media_requests_for_blackbox_verification(self):
        for path in ("src/httpserver.cpp", "src/posix_httpserver.cpp"):
            source = self.read(path)
            self.assertIn("HTTP request: src=%hs method=%hs path=%hs", source)
            self.assertIn('path.rfind("/media/", 0) == 0', source)


if __name__ == "__main__":
    unittest.main()
