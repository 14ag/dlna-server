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
            "DLNA Server HTTP TCP",
            "DLNA Server SSDP UDP",
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
            "AddRule(rules, kTcpRuleName, kProtocolTcp, 0, false",
            "AddRule(rules, kUdpRuleName, kProtocolUdp, kSsdpPort, true",
            "put_LocalPorts",
        ):
            self.assertIn(token, source)

        self.assertIn("firewall_access_win.cpp", cmake)
        self.assertIn("--configure-firewall", main)
        self.assertIn("ConfigureFirewallAccessElevated", main)
        self.assertIn("EnsureFirewallAccess(AppConfig.port, FirewallAccessMode::Interactive", server)

    def test_windows_settings_port_change_restarts_without_firewall_churn(self):
        source = self.read("src/mainwindow.cpp")

        for token in (
            "int oldPort = AppConfig.port",
            "SettingsDialog::Show(hwnd)",
            "m_isRunning && AppConfig.port != oldPort",
            "DLNAServer.Stop()",
            "DLNAServer.Start()",
            "Restart failed",
        ):
            self.assertIn(token, source)

        self.assertNotIn("EnsureFirewallAccess(AppConfig.port", source)

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

    def test_android_smoke_requires_firewall_rules_and_real_vlc_playback(self):
        script = self.read("tests/verify-android-smoke.ps1")

        for token in (
            "New-TestWav",
            "Ensure-FirewallAccess",
            "Test-IsAdmin",
            "PowerShell is not elevated",
            "org.videolan.vlc/.StartActivity",
            "uiautomator",
            "ContentDirectory root browse",
            "Android range GET expected 206",
            "SSDP search in: src=",
            "HTTP request: src=",
        ):
            self.assertIn(token, script)

        self.assertNotIn("fake mp3 bytes", script)
        self.assertIn("--configure-firewall", script)

    def test_http_debug_log_records_media_requests_for_blackbox_verification(self):
        source = self.read("src/httpserver.cpp")

        self.assertIn("HTTP request: src=%hs method=%hs path=%hs", source)
        self.assertIn('path.rfind("/media/", 0) == 0', source)


if __name__ == "__main__":
    unittest.main()
