import pathlib

SOURCE_PATH = pathlib.Path(__file__).resolve().parent.parent / "src" / "gtk4_gui_main.cpp"
CSS_PATH = pathlib.Path(__file__).resolve().parent.parent / "resources" / "gtk" / "style.css"


def read_source():
    return SOURCE_PATH.read_text(encoding="utf-8")


def test_main_window_uses_shared_win10_titlebar():
    text = read_source()
    assert 'CreateWin10Titlebar(GTK_WINDOW(window), "DLNA Server", WindowChrome::Main)' in text
    assert "g_title =" not in text


def test_sharp_corner_css_present():
    text = CSS_PATH.read_text(encoding="utf-8")
    # the generated template emits border-radius: 0 (unitless zero is
    # valid CSS); accept either spelling so the assertion stays robust
    assert "border-radius: 0" in text


def test_shared_titlebar_owns_the_only_window_controls():
    text = read_source()
    helper = text[text.index("GtkWidget* CreateWin10Titlebar"):text.index("GtkWindow* CreateMessageWindow")]
    assert 'g_object_set(titlebar, "show-title-buttons", FALSE, nullptr)' in helper
    assert '":minimize,close"' in helper
    assert '":close"' in helper
    assert helper.count("gtk_window_controls_new") == 1


def test_blue_accent_hover_rule_present():
    text = CSS_PATH.read_text(encoding="utf-8")
    assert "outline: 1px solid @focus_color" in text


def test_settings_toolbar_spacing_constant_present():
    text = read_source()
    assert "kSettingsToolbarButtonWidth" in text
    assert "UiTokens::kSettingsServerGroupX + 64" not in text


def test_source_list_border_present():
    text = CSS_PATH.read_text(encoding="utf-8")
    assert ".source-list { background-color: @page_color; border: 1px solid @border_color; border-radius: 0; }" in text
