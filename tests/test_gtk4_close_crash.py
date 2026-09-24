import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent


def test_non_row_child_guard_present():
    cpp = (REPO_ROOT / "src/gtk4_gui_main.cpp").read_text(encoding="utf-8")
    assert "GTK_IS_LIST_BOX_ROW(rowWidget)" in cpp


def test_context_menu_popdown_before_destroy_present():
    cpp = (REPO_ROOT / "src/gtk4_gui_main.cpp").read_text(encoding="utf-8")
    assert "g_activeSourceContextMenu" in cpp
    assert "gtk_popover_popdown(GTK_POPOVER(g_activeSourceContextMenu))" in cpp


@pytest.mark.gui_only
@pytest.mark.needs_xvfb
def test_main_window_closes_with_context_menu_open(gtk_binary, xvfb_env, tmp_path):
    config_dir = tmp_path / "config"
    config_dir.mkdir()
    env = dict(xvfb_env)
    env["XDG_CONFIG_HOME"] = str(config_dir)
    env["HOME"] = str(config_dir)
    runtime_dir = tmp_path / "runtime"
    runtime_dir.mkdir()
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)

    proc = subprocess.run(
        [gtk_binary, "--print-close-with-context-menu-open"],
        env=env, capture_output=True, text=True, timeout=30,
    )
    assert "Tried to remove non-child" not in proc.stderr, proc.stderr
    assert proc.returncode == 0


def test_native_tooltip_removed_from_source_rows():
    cpp = (REPO_ROOT / "src/gtk4_gui_main.cpp").read_text(encoding="utf-8")
    build_row_start = cpp.index("GtkWidget* BuildSourceRow(")
    build_row_end = cpp.index("\n}\n", build_row_start)
    build_row_body = cpp[build_row_start:build_row_end]
    assert "gtk_widget_set_tooltip_text" not in build_row_body


def test_custom_hover_tip_still_present():
    cpp = (REPO_ROOT / "src/gtk4_gui_main.cpp").read_text(encoding="utf-8")
    assert "InstallSourceListHoverTip" in cpp
    assert "dlna-hover-tip" in cpp
