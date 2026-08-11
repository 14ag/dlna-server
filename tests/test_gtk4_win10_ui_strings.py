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


def test_main_window_titlebar_set():
    text = read_source()
    assert "gtk_window_set_titlebar(GTK_WINDOW(window), mainHeaderBar)" in text
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
