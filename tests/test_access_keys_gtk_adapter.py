import subprocess

import pytest


def _print_gtk_label(dlna_binary, label):
    result = subprocess.run(
        [dlna_binary, "--print-gtk-mnemonic-label", label],
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    return result.stdout.strip()


class TestAccessKeysGtkAdapter:
    def test_single_ampersand_becomes_underscore(self, dlna_binary):
        assert _print_gtk_label(dlna_binary, "&Add") == "_Add"

    def test_double_ampersand_becomes_single(self, dlna_binary):
        assert _print_gtk_label(dlna_binary, "A&&B") == "A&B"

    def test_no_ampersand_unchanged(self, dlna_binary):
        assert _print_gtk_label(dlna_binary, "Settings") == "Settings"

    def test_multiple_markers(self, dlna_binary):
        assert _print_gtk_label(dlna_binary, "&A&B") == "_A_B"
        assert _print_gtk_label(dlna_binary, "A&&B&C&") == "A&B_C_"
