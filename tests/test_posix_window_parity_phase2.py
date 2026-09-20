import pathlib
import re
import subprocess

import pytest

REPO = pathlib.Path(__file__).resolve().parents[1]

def test_source_list_stroke_matches_win32_border_token():
    css = (REPO / "resources/gtk/figma.css").read_text()
    block = re.search(r"scrolledwindow\.source-list\s*\{[^}]*\}", css, re.S).group(0)
    assert "#585858" in block, "list stroke must match UiTokens::kBorderColor rgb(88,88,88)"
    assert "#313131" not in block

def test_source_list_focus_ring_matches_win32():
    css = (REPO / "resources/gtk/figma.css").read_text()
    assert "scrolledwindow.source-list:focus-within" in css
    assert "#60a5fa" in css

def test_source_list_selection_matches_win32_highlight():
    css = (REPO / "resources/gtk/figma.css").read_text()
    assert "#0078d7" in css

def test_ui_token_border_is_still_88():
    tokens = (REPO / "src/ui_tokens.h").read_text()
    assert "kBorderColor = { 88, 88, 88 }" in tokens

@pytest.mark.posix_only
@pytest.mark.needs_xvfb
def test_message_box_parents_follow_the_modal_stack(gui_binary, xvfb_env):
    out = subprocess.run([gui_binary, "--dump-msgbox-parent"],
                         capture_output=True, text=True, env=xvfb_env, timeout=120).stdout
    parents = re.findall(r"\[gtk4-msgbox-parent\] parent=(\w+)", out)
    assert parents[0] == "main"
    assert parents[1] == "settings"
    assert parents[-1] == "log"

@pytest.mark.posix_only
@pytest.mark.needs_xvfb
def test_settings_reopens_after_close(gui_binary, xvfb_env):
    out = subprocess.run([gui_binary, "--print-settings-reopen"],
                         capture_output=True, text=True, env=xvfb_env, timeout=120).stdout
    assert "first-open=1" in out
    assert "slot-cleared=1" in out
    assert "second-open=1" in out
