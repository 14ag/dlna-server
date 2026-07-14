import unittest
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UiSpecTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_ui_spec_covers_cross_platform_contract(self):
        spec = self.read("docs/ui-spec.md")

        for text in (
            "Windows Win32 UI and Linux FLTK UI",
            "Default main window size: 440 x 600",
            "Minimum content size: 440 x 460",
            "Toolbar height: 56 px",
            "Status strip height: 40 px",
            "Modal child windows restore focus to their owner only when the app still owns the foreground chain",
            "Windows UI font: Segoe UI Variable with Segoe UI fallback",
            "Windows dialog templates use 10 pt Segoe UI",
            "Parent surfaces own layout",
            "Toolbar buttons are 32 px tall",
            "8 px corner radius",
            "4 px increments",
            "Add media folder",
            "Delete selected source",
            "Start server",
            "Stop server",
            "Settings",
            "Default playlist",
            "UPnP device icons",
            "Log text is read-only",
            "No clipped labels",
        ):
            self.assertIn(text, spec)

    def test_release_version_is_1_3_0(self):
        cmake = self.read("CMakeLists.txt")
        changelog = self.read("CHANGELOG.md")

        self.assertIn("project(dlna-server VERSION 1.4.0)", cmake)
        self.assertIn("## [1.4.0] - 2026-05-29", changelog)

    def test_modal_focus_policy_is_foreground_chain_gated(self):
        main = self.read("src/mainwindow.cpp")
        settings = self.read("src/settingsdlg.cpp")
        logdlg = self.read("src/logdlg.cpp")
        helper_header = self.read("src/modal_focus.h")
        helper_impl = self.read("src/modal_focus.cpp")
        cmake = self.read("CMakeLists.txt")

        for token in (
            "struct ModalFocusSnapshot",
            "CaptureModalFocus",
            "EnableOwnerAndRestoreModalFocus",
            "RestoreModalFocus",
            "IsWindowInModalChain",
        ):
            self.assertIn(token, helper_header + helper_impl)
        self.assertIn("src/modal_focus.cpp", cmake)
        self.assertIn("src/modal_focus.h", cmake)
        self.assertIn("GetForegroundWindow()", helper_impl)
        self.assertIn("GetWindow(hwnd, GW_OWNER)", helper_impl)
        self.assertIn("SetActiveWindow(targetRoot)", helper_impl)
        self.assertIn("SetFocus(focusTarget)", helper_impl)
        self.assertNotIn("SetForegroundWindow", helper_header + helper_impl)

        for source in (main, settings, logdlg):
            self.assertIn('#include "modal_focus.h"', source)
            self.assertIn("CaptureModalFocus", source)
            self.assertIn("RestoreModalFocus", source)

        for token in (
            "state->focusSnapshot",
            "RestoreModalFocus(state->focusSnapshot, state->edit)",
            "EnableOwnerAndRestoreModalFocus(state->focusSnapshot, state->owner)",
            "if (!state.done)",
            "if (IsWindow(hwnd)) DestroyWindow(hwnd)",
        ):
            self.assertIn(token, main)
        source_prompt_block = main[main.index("std::wstring PromptForMediaSource"):main.index("MainWindow::MainWindow")]
        self.assertNotIn("SetForegroundWindow(owner)", source_prompt_block)

        for token in (
            "state->focusSnapshot",
            "RestoreModalFocus(state->focusSnapshot, GetDlgItem(hwnd, targetId))",
            "EnableOwnerAndRestoreModalFocus(state->focusSnapshot, state->owner)",
            "RestoreModalFocus(focusSnapshot, hwndDlg)",
            "if (!state.done)",
            "if (IsWindow(hwnd)) DestroyWindow(hwnd)",
        ):
            self.assertIn(token, settings)
        playlist_block = settings[settings.index("void SettingsDialog::ShowPlaylistEntryForm"):settings.index("INT_PTR CALLBACK SettingsDialog::DialogProc")]
        self.assertNotIn("SetForegroundWindow(hwndDlg)", playlist_block)

        self.assertIn("ModalFocusSnapshot focusSnapshot = CaptureModalFocus(hParent)", logdlg)
        self.assertIn("RestoreModalFocus(focusSnapshot, hParent)", logdlg)


if __name__ == "__main__":
    unittest.main()
