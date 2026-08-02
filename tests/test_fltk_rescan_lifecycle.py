import re
from pathlib import Path

FLTK_SOURCE = Path(__file__).parent.parent / "src" / "fltk_gui_main.cpp"


def test_begin_rescan_does_not_detach():
    text = FLTK_SOURCE.read_text()
    # Find BeginRescan's body specifically, not the whole file, so this
    # test does not false-fail on an unrelated .detach() call elsewhere
    # (e.g. this project intentionally keeps other detach()-based
    # background listeners in other files).
    match = re.search(r"void BeginRescan\(\) \{.*?\n    \}\n", text, re.DOTALL)
    assert match, "BeginRescan() not found in fltk_gui_main.cpp -- update this test's regex if the method was reformatted"
    body = match.group(0)
    assert ".detach()" not in body, "BeginRescan must not detach its worker thread (F-CMR-02 regression)"
    assert "m_rescanWorker" in body, "BeginRescan must use the joinable m_rescanWorker member (F-CMR-02)"


def test_destructor_joins_rescan_worker():
    text = FLTK_SOURCE.read_text()
    match = re.search(r"~MainWindow\(\) override \{.*?\n    \}\n", text, re.DOTALL)
    assert match, "~MainWindow() not found -- update this test's regex if the destructor was reformatted"
    body = match.group(0)
    assert "m_rescanWorker.joinable()" in body and "m_rescanWorker.join()" in body, \
        "~MainWindow() must join m_rescanWorker before returning (F-CMR-02)"
