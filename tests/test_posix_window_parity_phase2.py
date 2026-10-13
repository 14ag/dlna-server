import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[1]

def test_source_list_stroke_matches_win32_border_token():
    css = (REPO / "resources/gtk/styles0.css").read_text()
    block = re.search(r"scrolledwindow\.source-list\s*\{[^}]*\}", css, re.S).group(0)
    assert "#585858" in block, "list stroke must match UiTokens::kBorderColor rgb(88,88,88)"
    assert "#313131" not in block

def test_source_list_focus_ring_matches_win32():
    css = (REPO / "resources/gtk/styles0.css").read_text()
    assert "scrolledwindow.source-list:focus-within" in css
    assert "#60a5fa" in css

def test_source_list_selection_matches_win32_highlight():
    css = (REPO / "resources/gtk/styles0.css").read_text()
    assert "#0078d7" in css

def test_ui_token_border_is_still_88():
    tokens = (REPO / "src/ui_tokens.h").read_text()
    assert "kBorderColor = { 88, 88, 88 }" in tokens
