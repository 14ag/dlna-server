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
    wmclass_match = re.search(r"StartupWMClass=([A-Za-z0-9_.-]+)", desktop_cmake)
    assert code_match and wmclass_match
    assert code_match.group(1) == wmclass_match.group(1)


@pytest.mark.posix_only
def test_win10_titlebar_contract(repo_root):
    code = (repo_root / SOURCE).read_text()
    css = (repo_root / "resources" / "gtk" / "style.css").read_text()
    helper = code[code.index("GtkWidget* CreateWin10Titlebar"):code.index("GtkWindow* CreateMessageWindow")]
    assert 'gtk_window_set_icon_name(window, "dlna-server")' in helper
    assert 'g_object_set(titlebar, "show-title-buttons", FALSE, nullptr)' in helper
    assert 'background-color: @win10_active_titlebar_color;' in css
    assert 'background-color: @win10_inactive_titlebar_color;' in css
    assert "rgb(49,49,49)" in css
    assert "rgb(50,50,50)" in css
