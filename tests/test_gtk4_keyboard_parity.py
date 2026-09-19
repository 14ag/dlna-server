import pytest

SOURCE = "src/gtk4_gui_main.cpp"


@pytest.mark.posix_only
def test_toolbar_group_navigation_matches_win32(repo_root):
    code = (repo_root / SOURCE).read_text()
    assert "OnMainWindowKeyPressed" in code
    assert "GDK_KEY_Left" in code and "GDK_KEY_Right" in code
    # Up/Down are swallowed on toolbar buttons, as ToolbarButtonProc does.
    assert "GDK_KEY_Up || keyval == GDK_KEY_Down" in code
    assert "GTK_PHASE_CAPTURE" in code


@pytest.mark.posix_only
def test_function_keys_use_the_shared_decision_table(repo_root):
    code = (repo_root / SOURCE).read_text()
    assert '#include "function_key_action.h"' in code
    assert "DecideFunctionKeyAction(" in code
    assert "GDK_KEY_F1" in code and "GDK_KEY_F5" in code


@pytest.mark.posix_only
def test_delete_gating_has_exactly_one_predicate(repo_root):
    code = (repo_root / SOURCE).read_text()
    assert "bool CanRemoveSelectedSource()" in code
    # Button, context menu and keyboard shortcut all consult the same function.
    assert code.count("CanRemoveSelectedSource()") >= 4


@pytest.mark.posix_only
def test_source_list_shows_runtime_override_like_win32(repo_root):
    code = (repo_root / SOURCE).read_text()
    refresh = code[code.index("void RefreshSourceList()"):code.index("void SaveSourcesFromList()")]
    assert "IsShowingOverrideSources()" in refresh
    assert "AppConfig.GetRuntimeSourceOverride()" in refresh
    save = code[code.index("void SaveSourcesFromList()"):]
    assert "if (IsShowingOverrideSources()) return;" in save[:400]
