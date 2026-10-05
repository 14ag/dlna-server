import pathlib

SOURCE_PATH = pathlib.Path(__file__).resolve().parent.parent / "src" / "gtk4_gui_main.cpp"
WIN32_SOURCE_PATH = pathlib.Path(__file__).resolve().parent.parent / "src" / "mainwindow.cpp"
CSS_PATH = pathlib.Path(__file__).resolve().parent.parent / "resources" / "gtk" / "style.css"


def read_source():
    return SOURCE_PATH.read_text(encoding="utf-8")


def read_win32_source():
    return WIN32_SOURCE_PATH.read_text(encoding="utf-8")


def test_main_window_uses_shared_win10_titlebar():
    text = read_source()
    assert 'CreateWin10Titlebar(GTK_WINDOW(window), "DLNA Server", WindowChrome::Main)' in text
    assert "g_title =" not in text


def test_sharp_corner_css_present():
    text = CSS_PATH.read_text(encoding="utf-8")
    # the generated template emits border-radius: 0 (unitless zero is
    # valid CSS); accept either spelling so the assertion stays robust
    assert "border-radius: 0" in text


def test_windows_source_list_has_full_path_hover_tooltip_without_hscroll():
    text = read_win32_source()
    listbox_block = text[text.index("m_hListSources = CreateWindowExW"):text.index("OleInitialize(NULL)")]
    assert "WS_HSCROLL" not in listbox_block
    assert "CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW" in text
    assert "TTM_SETDELAYTIME" in text
    assert "TTM_UPDATETIPTEXTW" in text


def test_blue_accent_hover_rule_present():
    text = CSS_PATH.read_text(encoding="utf-8")
    assert "outline: 1px solid @focus_color" in text


def test_source_list_border_present():
    text = CSS_PATH.read_text(encoding="utf-8")
    assert ".source-list { background-color: @page_color; border: 1px solid @border_color; border-radius: 0; }" in text
