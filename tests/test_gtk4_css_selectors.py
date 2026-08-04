import pathlib

SRC = pathlib.Path(__file__).resolve().parents[1] / "src" / "gtk4_gui_main.cpp"


def _read_source() -> str:
    return SRC.read_text(encoding="utf-8")


def test_no_stray_listbox_type_selector():
    """
    GTK4 CSS selectors match CSS node names, not C type names.
    GtkListBox's CSS node name is 'list', not 'listbox'.
    A bare 'listbox' selector never matches anything and is always a bug.
    """
    text = _read_source()
    assert '"listbox {' not in text
    assert '"listbox row' not in text


def test_source_list_uses_correct_gtklistbox_node_names():
    """
    The dark-theme rules for the media-source list must target the real
    GtkListBox/GtkListBoxRow CSS node names ('list' / 'row'), scoped under
    the '.source-list' class already applied to the containing
    GtkScrolledWindow in BuildMainWindow().
    """
    text = _read_source()
    assert ".source-list list {" in text
    assert ".source-list row {" in text
    assert ".source-list row:selected {" in text
