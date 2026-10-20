import pathlib
import pytest

pytestmark = pytest.mark.posix_only

SRC = pathlib.Path(__file__).resolve().parents[1] / "src" / "gtk4_gui_main.cpp"
CSS = pathlib.Path(__file__).resolve().parents[1] / "resources" / "gtk" / "styles1.css"
STYLES0 = pathlib.Path(__file__).resolve().parents[1] / "resources" / "gtk" / "styles0.css"


def _read_css() -> str:
    return CSS.read_text(encoding="utf-8")


def test_context_menu_items_have_no_inner_border():
    text = STYLES0.read_text(encoding="utf-8")
    block = text[text.index("popover.menu button.model,"):]
    block = block[:block.index("}")]
    assert "border: none;" in block
    assert "background-color: transparent;" in block


def test_source_list_uses_correct_gtklistbox_node_names():
    """
    The dark-theme rules for the media-source list must target the real
    GtkListBox/GtkListBoxRow CSS node names ('list' / 'row'), scoped under
    the '.source-list' class already applied to the containing
    GtkScrolledWindow in BuildMainWindow().
    """
    text = _read_css()
    assert ".source-list list {" in text
    assert ".source-list row {" in text
    assert ".source-list row:selected {" in text
