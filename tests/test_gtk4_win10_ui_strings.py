import pathlib

SOURCE_PATH = pathlib.Path(__file__).resolve().parent.parent / "src" / "gtk4_gui_main.cpp"


def read_source():
    return SOURCE_PATH.read_text(encoding="utf-8")


def test_title_label_text_removed():
    text = read_source()
    assert 'g_title = gtk_label_new("")' in text
    assert "gtk_widget_set_visible(g_title, FALSE)" in text


def test_sharp_corner_css_present():
    text = read_source()
    assert "border-radius: 0px" in text


def test_main_window_has_no_custom_titlebar():
    """The main window has no custom header bar (Task 15: show_title_buttons(FALSE)
    caused 3 extra window control buttons, so the header bar was removed)."""
    text = read_source()
    assert "gtk_window_set_titlebar(GTK_WINDOW(window), mainHeaderBar)" not in text
    # win10-titlebar class is used only on the Settings dialog CSD header bar
    assert "win10-titlebar" in text


def test_blue_accent_hover_rule_present():
    text = read_source()
    assert "border-bottom: 2px solid rgb(96,165,250)" in text


def test_settings_toolbar_spacing_constant_present():
    text = read_source()
    assert "kSettingsToolbarButtonWidth" in text
    assert "UiTokens::kSettingsServerGroupX + 64" not in text


def test_source_list_border_present():
    text = read_source()
    assert "border: 1px solid rgb(88,88,88)" in text
