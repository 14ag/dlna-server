import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

def read_source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def get_source_bundle(*paths: str) -> str:
    return "\n".join(read_source(path) for path in paths)



class ReviewSixJuneSourceContracts(unittest.TestCase):

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

    def test_debug_logs_are_written_beside_config_on_all_platforms(self):
        windows_log = read_source("src/log.cpp")
        posix_log = read_source("src/posix_log.cpp")
        config_header = read_source("src/config.h")
        bug_template = read_source(".github/ISSUE_TEMPLATE/bug_report.md")

        self.assertIn("std::wstring GetConfigPath()", config_header)
        self.assertIn("AppConfig.GetConfigPath()", windows_log)
        self.assertIn('PathAppendW(szPath, L"debug.log")', windows_log)
        self.assertNotIn("CSIDL_APPDATA", windows_log)
        self.assertNotIn("SHGetFolderPathW", windows_log)

        self.assertIn("AppConfig.GetConfigPath()", posix_log)
        self.assertIn('"debug.log"', posix_log)
        self.assertIn("std::FILE* g_debugLogFile", posix_log)
        self.assertIn("AppConfig.IsDebugLogEnabled()", posix_log)

        self.assertIn("beside `config.ini`", bug_template)
        self.assertNotIn("WinDLNAServer", bug_template)


if __name__ == "__main__":
    unittest.main()
