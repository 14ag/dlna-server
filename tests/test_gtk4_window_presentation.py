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
    assert "CreateWin10WindowControl" in helper
    assert "gtk_window_controls_new" not in helper
    assert 'background-color: @win10_active_titlebar_color;' in css
    assert 'background-color: @win10_inactive_titlebar_color;' in css
    assert "rgb(49,49,49)" in css
    assert "rgb(50,50,50)" in css


@pytest.mark.posix_only
def test_titlebar_uses_custom_windows_controls(repo_root):
    code = (repo_root / SOURCE).read_text()
    helper = code[code.index("GtkWidget* CreateWin10Titlebar"):code.index("GtkWindow* CreateMessageWindow")]
    assert "CreateWin10WindowControl" in helper
    assert "gtk_window_controls_new" not in helper
    assert '"win10-minimize-control"' in code
    assert '"win10-close-control"' in code

    css = (repo_root / "resources" / "gtk" / "figma.css").read_text()
    assert "penpot-minimize.svg" in css
    assert "penpot-close.svg" in css


@pytest.mark.posix_only
def test_penpot_surface_tokens_and_windows_assets(repo_root):
    css = (repo_root / "resources" / "gtk" / "figma.css").read_text()
    for color in ("#191919", "#1f1f1f", "#252525", "#f0f0f0", "#333333"):
        assert color in css
    assert 'font-family: "Segoe UI Variable Text", "Segoe UI", sans-serif;' in css

    assets = repo_root / "resources" / "gtk"
    for name in (
        "penpot-minimize.svg",
        "penpot-maximize.svg",
        "penpot-close.svg",
        "penpot-warning.svg",
    ):
        assert (assets / name).is_file()


@pytest.mark.posix_only
def test_main_window_chrome_matches_penpot_without_gtk_chrome(repo_root):
    code = (repo_root / SOURCE).read_text()
    header = (repo_root / "src" / "ui_tokens_posix.h").read_text()
    css = (repo_root / "resources" / "gtk" / "figma.css").read_text()

    assert "kMainWindowBodyHeight = 563" in header
    assert "gtk_window_set_decorated(window, FALSE);" not in code
    assert "gtk_window_set_titlebar(" in code
    assert '"win10-minimize-control"' in code
    assert '"win10-maximize-control"' in code
    assert '"win10-close-control"' in code
    assert "gtk_window_controls_new" not in code
    assert "gtk_header_bar_new" not in code
    assert "penpot-minimize.svg" in css
    assert "penpot-maximize.svg" in css
    assert "penpot-close.svg" in css
    assert "#191919" in css
    assert "#252525" in css
    assert "#1f1f1f" in css
    assert "border: 1px solid #3b3b3b" in css


@pytest.mark.posix_only
def test_remaining_window_bounds_match_penpot(repo_root):
    header = (repo_root / "src" / "ui_tokens_posix.h").read_text()

    for token in (
        "kSettingsWindowWidth = 700", "kSettingsWindowHeight = 797",
        "kLogWindowWidth = 772", "kLogWindowHeight = 712",
        "kHelpWindowWidth = 530", "kHelpWindowHeight = 400",
        "kSourcePromptWindowWidth = 538", "kSourcePromptWindowHeight = 209",
        "kPlaylistWindowWidth = 538", "kPlaylistWindowHeight = 189",
        "kWarningWindowWidth = 245", "kWarningWindowHeight = 150",
        "kSettingsWindowBodyHeight = 767", "kLogWindowBodyHeight = 682",
        "kHelpWindowBodyHeight = 370", "kSourcePromptWindowBodyHeight = 179",
        "kPlaylistWindowBodyHeight = 159", "kWarningWindowBodyHeight = 120",
        "kPlaylistMovieEditX = 113", "kPlaylistAddX = 445",
        "kSettingsServerNameLabelX = 41", "kSettingsServerNameEditX = 195",
        "kLogTextX = 19", "kLogRefreshX = 528",
    ):
        assert token in header


@pytest.mark.posix_only
def test_win10_window_chrome_uses_profile_border_shadow_and_focus_state(repo_root):
    code = (repo_root / SOURCE).read_text()
    css = (repo_root / "resources" / "gtk" / "figma.css").read_text()

    assert '"win10-main-titlebar"' in code
    assert '"win10-dialog-titlebar"' in code
    assert '"dlna-log-workspace"' in code
    assert "border: 1px solid #606060" in css
    assert "box-shadow: 0 6px 22px 9px rgba(0, 0, 0, 0.38)" in css
    assert ".win10-titlebar.win10-inactive button.win10-minimize-control" in css
    assert ".win10-titlebar.win10-inactive button.win10-close-control" in css
    assert ".dlna-groupbox > label" in css
    assert ".dlna-log-workspace" in css


@pytest.mark.posix_only
def test_source_list_has_no_horizontal_scrollbar_and_full_path_tooltips(repo_root):
    code = (repo_root / SOURCE).read_text()
    css = (repo_root / "resources" / "gtk" / "figma.css").read_text()

    assert "GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC" in code
    # One construction point for every source row.
    assert "GtkWidget* BuildSourceRow(const std::string& pathUtf8)" in code
    assert code.count("gtk_list_box_row_new()") == 1
    assert "PANGO_ELLIPSIZE_END" in code
    assert "gtk_widget_set_tooltip_text(rowLabel, pathUtf8.c_str())" in code
    # GTK4 cannot shorten the tooltip delay, so the immediate tip is a popover.
    assert "InstallSourceListHoverTip" in code
    assert "pango_layout_is_ellipsized" in code
    assert 'font-family: "Segoe UI Variable Text", "Segoe UI", sans-serif;' in css
    assert "font-size: 14px" in css
    assert ".dlna-log-body" in css
    assert "window.dlna-log-window" in css
