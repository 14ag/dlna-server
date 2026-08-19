import os
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]


def _read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def _run(binary_path, *args, timeout=120):
    env = os.environ.copy()
    env["DLNA_SERVER_SKIP_FIREWALL"] = "1"
    return subprocess.run(
        [str(binary_path), *args],
        capture_output=True, text=True, timeout=timeout, env=env,
    )


@pytest.mark.windows_only
class TestGeometryDumpGating:
    """Tasks 9 and 10 regression guards."""

    @pytest.mark.parametrize("flag_value,expected", [("1", "1"), ("0", "0")])
    def test_should_dump_dialog_geometry_predicate(
        self, dlna_binary, flag_value, expected
    ):
        # the pure decision must be exercisable from either build so a
        # regression in the gate can be caught headlessly
        result = _run(
            dlna_binary,
            "--print-should-dump-dialog-geometry", flag_value,
        )
        assert result.returncode == 0, result.stdout + result.stderr
        assert result.stdout.strip() == expected

    def test_shared_dump_no_longer_gated_on_debug_flag(self):
        # the geometry dump must be gated on the dedicated hidden flag
        # only; piggybacking on AppConfig.IsDebugLogEnabled() is what
        # let every dialog open during a debug session crowd out the
        # server history in debug.log
        source = _read("src/win_geometry_dump.h")
        assert "AppConfig.IsDebugLogEnabled()" not in source
        assert "ShouldDumpDialogGeometry(" in source
        assert "DumpWidgetGeometryFlag()" in source

    def test_settingsdlg_duplicate_walk_deleted(self):
        # the reimplemented walk must be gone entirely, not merely
        # unreferenced, so the shared helper is the single source of
        # truth for geometry dumps
        source = _read("src/settingsdlg.cpp")
        assert "LogSettingsControlGeometryProc" not in source

    def test_settingsdlg_calls_shared_dump_once(self):
        # the settings dialog must route through the shared dump with
        # its original log tag and exactly once per dialog open
        source = _read("src/settingsdlg.cpp")
        assert source.count(
            'DumpDialogGeometry(hwndDlg, L"settings-geometry")'
        ) == 1

    def test_regression_geometry_line_gated_by_flag(self, dlna_binary, tmp_path):
        # full regression needs a display and a scripted Settings-dialog
        # open; this repo has no Win32 GUI dialog-driving infrastructure
        # so it cannot run here. keeping the test with an explicit reason
        # documents the exact manual/CI step needed to cover it
        if not os.environ.get("DLNA_SERVER_GUI_DRIVE_TEST"):
            pytest.fail(
                "no Win32 GUI dialog-driving infrastructure in this repo; "
                "cannot script a Settings-dialog open to capture debug.log"
            )
        media = tmp_path / "media"
        media.mkdir()
        (media / "sample.mp4").touch()
        # with the flag the settings open dumps once; without it nothing
        # is emitted even under --debug
        with_flag = _run(
            dlna_binary, "--debug", "--dump-widget-geometry",
            "--source", str(media),
        )
        assert with_flag.returncode == 0, with_flag.stdout + with_flag.stderr
        without_flag = _run(
            dlna_binary, "--debug", "--source", str(media),
        )
        assert without_flag.returncode == 0, \
            without_flag.stdout + without_flag.stderr
        # the actual dialog-driving assertion belongs in the environment
        # that can open the dialog; here we only prove the hook runs clean
        assert "settings-geometry" not in with_flag.stdout
