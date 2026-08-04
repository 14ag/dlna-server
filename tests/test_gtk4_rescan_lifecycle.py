import re
from pathlib import Path

GTK4_SOURCE = Path(__file__).parent.parent / "src" / "gtk4_gui_main.cpp"


def test_begin_rescan_does_not_detach():
    text = GTK4_SOURCE.read_text()
    # Find BeginRescan's body specifically, not the whole file, so this
    # test does not false-fail on an unrelated .detach() call elsewhere
    # (e.g. this project intentionally keeps other detach()-based
    # background listeners in other files).
    match = re.search(r"void BeginRescan\(\) \{.*?\n\}\n", text, re.DOTALL)
    assert match, "BeginRescan() not found in gtk4_gui_main.cpp -- update this test's regex if the method was reformatted"
    body = match.group(0)
    assert ".detach()" not in body, "BeginRescan must not detach its worker thread (F-CMR-02 regression)"
    assert "g_rescanWorker" in body, "BeginRescan must use the joinable g_rescanWorker (F-CMR-02)"


def test_main_joins_rescan_worker_on_shutdown():
    text = GTK4_SOURCE.read_text()
    # GTK4 has no MainWindow destructor; cleanup happens in main(). The
    # rescan worker must be joined there before the process exits.
    match = re.search(r"int main\(int argc, char\*\* argv\) \{.*?return result;\n\}", text, re.DOTALL)
    assert match, "main() not found -- update this test's regex if the entry point was reformatted"
    body = match.group(0)
    assert "g_rescanWorker.joinable()" in body and "g_rescanWorker.join()" in body, \
        "main() must join g_rescanWorker before returning (F-CMR-02)"
