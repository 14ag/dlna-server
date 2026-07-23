"""
Contract tests for POSIX XDG-compliant config path resolution.

Validates that the C++ source implements the expected XDG config path
resolver, including legacy config migration and the --print-config-path
diagnostic hook.
"""

from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def _source(name: str) -> str:
    return (ROOT / "src" / name).read_text(encoding="utf-8")


# ---- posix_config.cpp ----

class TestConfigPathResolver:
    def test_defines_resolve_home_directory(self):
        """Must have ResolveHomeDirectory() that reads $HOME or getpwuid."""
        src = _source("posix_config.cpp")
        assert "ResolveHomeDirectory" in src
        assert 'std::getenv("HOME")' in src
        assert "getpwuid" in src

    def test_defines_resolve_xdg_config_base(self):
        """Must check $XDG_CONFIG_HOME, fall back to ~/.config."""
        src = _source("posix_config.cpp")
        assert "ResolveXdgConfigBase" in src
        assert 'std::getenv("XDG_CONFIG_HOME")' in src
        assert "/.config" in src

    def test_app_root_config_path_uses_xdg(self):
        """Must return $XDG_CONFIG_HOME/dlna-server/config.ini."""
        src = _source("posix_config.cpp")
        assert "AppRootConfigPath" in src
        assert "dlna-server" in src
        assert "config.ini" in src
        assert "create_directories" in src

    def test_migrate_legacy_config_if_present(self):
        """MigrateLegacyConfigIfPresent must exist and check FsExists."""
        src = _source("posix_config.cpp")
        assert "MigrateLegacyConfigIfPresent" in src
        assert "readlink(\"/proc/self/exe\"" in src
        assert "config.ini" in src

    def test_config_load_calls_migration(self):
        """Config::Load must invoke MigrateLegacyConfigIfPresent."""
        src = _source("posix_config.cpp")
        assert "MigrateLegacyConfigIfPresent(GetConfigPath())" in src


# ---- posix_main.cpp ----

class TestConfigPathDiagnostic:
    def test_print_config_path_flag(self):
        """posix_main.cpp must handle --print-config-path."""
        src = _source("posix_main.cpp")
        assert "--print-config-path" in src
        assert "GetConfigPath" in src

    def test_main_cpp_also_has_hook(self):
        """Windows main.cpp must also handle --print-config-path."""
        src = _source("main.cpp")
        assert "--print-config-path" in src
        assert "GetConfigPath" in src


# ---- config.h ----

class TestConfigHeader:
    def test_get_config_path_declared(self):
        """Config class must declare GetConfigPath()."""
        src = _source("config.h")
        assert "GetConfigPath" in src
