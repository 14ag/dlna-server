from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
APP_ID = "com.github.dlna-server-14ag"

OLD_LITERALS = [
    'L"dlna-server_Main"',
    'L"dlna-server_SingleInstance_Mutex"',
    'L"dlna-server_SourcePrompt"',
    'L"dlna-server_PlaylistEntry"',
    'L"dlna-server_HelpDialog"',
    '"dlna-server-14ag"',
    '"/tmp/dlna-server-"',
    '"/dlna-server.lock"',
    '"/dlna-server.sock"',
    'gtk_application_new("com.github',
    'g_variant_new_string("dlna-server")',
    'gtk_window_set_icon_name(window, "dlna-server")',
]

NEEDS_INCLUDE = [
    "main.cpp", "mainwindow.cpp", "settingsdlg.cpp", "help_dialog.cpp",
    "cli_print_hooks_win.cpp", "config.cpp", "posix_single_instance.cpp",
    "gtk4_gui_main.cpp", "posix_tray.cpp", "cli_print_hooks_posix.cpp",
]


def sources():
    return list(SRC.glob("*.cpp")) + list(SRC.glob("*.h"))


def test_old_internal_names_are_gone_from_src():
    for path in sources():
        text = path.read_text(encoding="utf-8", errors="ignore")
        for literal in OLD_LITERALS:
            assert literal not in text, f"{path.name}: {literal}"


def test_edited_files_include_identity_header():
    for name in NEEDS_INCLUDE:
        text = (SRC / name).read_text(encoding="utf-8", errors="ignore")
        assert '#include "app_identity.h"' in text, name


def test_win32_uses_macros_for_cross_process_names():
    main = (SRC / "main.cpp").read_text(encoding="utf-8")
    assert main.count("DLNA_MAIN_WINDOW_CLASS_W") == 3
    assert "DLNA_SINGLE_INSTANCE_MUTEX_W" in main
    hooks = (SRC / "cli_print_hooks_win.cpp").read_text(encoding="utf-8")
    assert "DLNA_MAIN_WINDOW_CLASS_W" in hooks
    assert "DLNA_SINGLE_INSTANCE_MUTEX_W" in hooks


def test_run_on_startup_value_is_branded_and_legacy_is_removed():
    text = (SRC / "config.cpp").read_text(encoding="utf-8")
    assert "RegSetValueExW(hKey, DLNA_APP_ID_W" in text
    assert 'RegDeleteValueW(hKey, L"dlna-server");' in text


def test_print_app_id_hook_exists_on_both_platforms():
    posix = (SRC / "cli_print_hooks_posix.cpp").read_text(encoding="utf-8")
    win = (SRC / "cli_print_hooks_win.cpp").read_text(encoding="utf-8")
    assert '"--print-app-id"' in posix
    assert 'L"--print-app-id"' in win


def test_startup_commands_are_not_branded():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "add_executable(dlna-server " in cmake
    assert "OUTPUT_NAME dlna-server-gui-bin" in cmake
    assert 'OUTPUT_NAME "DLNA Server"' in cmake
    assert APP_ID in cmake
