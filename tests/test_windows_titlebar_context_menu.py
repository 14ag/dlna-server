import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def test_main_window_forwards_non_list_context_menu():
    path = os.path.join(ROOT, "src", "mainwindow.cpp")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    block = text.split("case WM_CONTEXTMENU:", 1)[1].split("case WM_SHOW_EXISTING_INSTANCE:", 1)[0]
    assert "sourceHwnd == m_hListSources" in block
    assert "break;" in block
    assert block.count("return 0;") == 1
