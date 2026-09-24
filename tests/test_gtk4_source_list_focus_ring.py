from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def test_focus_ring_class_wired_in_cpp():
    cpp = (REPO_ROOT / "src/gtk4_gui_main.cpp").read_text(encoding="utf-8")
    assert "gtk_widget_add_css_class(g_sourcesScrolled, \"source-list-focused\")" in cpp
    assert "gtk_widget_remove_css_class(g_sourcesScrolled, \"source-list-focused\")" in cpp


def test_focus_ring_css_present():
    figma_css = (REPO_ROOT / "resources/gtk/figma.css").read_text(encoding="utf-8")
    assert "scrolledwindow.source-list" in figma_css
    assert "585858" in figma_css.upper()
    assert "60A5FA" in figma_css.upper()
    assert "source-list-focused" in figma_css
