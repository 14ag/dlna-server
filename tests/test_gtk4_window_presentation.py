import re
import pytest

SOURCE = "src/gtk4_gui_main.cpp"

@pytest.mark.posix_only
def test_build_main_window_ends_with_present(repo_root):
    text = (repo_root / SOURCE).read_text()
    match = re.search(r"void BuildMainWindow\(GtkApplication\* app\) \{(.*?)\n\}", text, re.S)
    assert match, "BuildMainWindow not found, update this test's anchor"
    body = match.group(1)
    assert "gtk_window_present(GTK_WINDOW(window));" in body
    assert "gtk_widget_hide(window)" not in body
    assert "gtk_widget_set_visible(window, FALSE)" not in body

@pytest.mark.posix_only
def test_gapplication_id_matches_desktop_file(repo_root):
    code = (repo_root / SOURCE).read_text()
    desktop_cmake = (repo_root / "packaging/linux/install_desktop.cmake.in").read_text()
    code_match = re.search(r'gtk_application_new\("([^"]+)"', code)
    icon_match = re.search(r"Icon=([A-Za-z0-9_.-]+)", desktop_cmake)
    assert code_match and icon_match
    assert code_match.group(1) == icon_match.group(1)
