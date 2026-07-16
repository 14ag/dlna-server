import subprocess
import pytest
from pathlib import Path


class TestCliFlagsSingleSource:

    def test_help_flag_works(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--help"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        output = result.stdout + result.stderr
        assert "--headless" in output

    def test_print_usage_not_hardcoded(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--help"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        output = result.stdout + result.stderr
        assert "DLNA Server.exe [--help]" in output

    def test_get_cli_flag_table_defined_in_header(self):
        src_dir = Path(__file__).resolve().parent.parent / "src"
        f = src_dir / "cli_flags.h"
        content = f.read_text("utf-8", errors="ignore")
        assert "GetCliFlagTable" in content
        assert "--headless" in content

    def test_help_dialog_no_hardcoded_flags(self):
        src_dir = Path(__file__).resolve().parent.parent / "src"
        content = (src_dir / "help_dialog.cpp").read_text(
            "utf-8", errors="ignore")
        for line in content.splitlines():
            ls = line.strip()
            if ls.startswith("//"):
                continue
            if ls.startswith("#"):
                continue
            if "\"--headless\"" in ls or "L\"--headless\"" in ls:
                pytest.fail(
                    "help_dialog.cpp must not hardcode flag strings; "
                    "uses GetCliFlagTable() instead")

    def test_cli_flags_table_printed_in_help(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--help"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        output = result.stdout + result.stderr
        assert "--source \"pathA\",\"pathB\"" in output
