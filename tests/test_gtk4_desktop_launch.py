from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def test_desktop_entry_has_absolute_exec_and_tryexec():
    text = (REPO_ROOT / "packaging/linux/install_desktop.cmake.in").read_text(encoding="utf-8")
    # absolute paths are hardcoded (not ${exec_path}): TryExec must hit the
    # real binary for WSLg taskbar binding, Exec the wrapper with the
    # GTK_LAUNCHED guard that breaks the gtk-launch re-entry loop
    assert "Exec=env DLNA_SERVER_GTK_LAUNCHED=1 /usr/bin/dlna-server-gui" in text
    assert "TryExec=/usr/bin/dlna-server-gui-bin" in text


def test_wrapper_script_has_directory_fallback_and_log():
    text = (REPO_ROOT / "packaging/linux/dlna-server-gui").read_text(encoding="utf-8")
    assert "resolve_script_dir" in text
    assert "gui-launch.log" in text
    assert "/usr/local/bin" in text
