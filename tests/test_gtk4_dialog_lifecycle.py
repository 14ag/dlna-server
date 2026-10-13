import pytest

SOURCE = "src/gtk4_gui_main.cpp"


@pytest.mark.posix_only
def test_every_modal_dialog_is_destroyed_when_its_loop_ends(repo_root):
    code = (repo_root / SOURCE).read_text()

    assert "void RunModalLoopAndDestroy(GtkWidget* dialog)" in code
    # Settings, Help and Log all run their loop through the shared helper.
    assert code.count("RunModalLoopAndDestroy(") >= 4

    # The settings guard must present, never silently no-op.
    settings = code[code.index("bool ShowSettingsDialog()"):]
    settings = settings[:settings.index("void LayoutMainWindow")]
    assert "gtk_window_present(GTK_WINDOW(g_settingsDialog));" in settings
    assert "if (g_settingsDialog != nullptr) return false;" not in settings

    # The single-pointer modal tracker is gone.
    assert "g_activeModal" not in code
    assert "ModalStack<GtkWindow*> g_modalStack;" in code


@pytest.mark.posix_only
def test_message_boxes_honour_escape_and_backspace(repo_root):
    code = (repo_root / SOURCE).read_text()
    show = code[code.index("void MessageBoxShow("):code.index("bool MessageBoxQuestion(")]
    question = code[code.index("bool MessageBoxQuestion("):code.index("void ShowFileChooserNativeImpl(")]
    assert "InstallSubwindowCloseKeys" in show
    assert "InstallSubwindowCloseKeys" in question
