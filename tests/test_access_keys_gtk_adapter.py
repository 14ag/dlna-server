import subprocess

import pytest


def _parse_mnemonics(output):
    result = []
    for token in output.strip().split(","):
        token = token.strip()
        result.append(token if token else "")
    return result


class TestAccessKeysGtkAdapter:
    def test_single_ampersand_becomes_underscore(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "&Add"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 1
        assert letters[0].lower() in "add", (
            f"Mnemonic '{letters[0]}' not found in '&Add'")

    def test_double_ampersand_becomes_single(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "A&&B"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 1
        assert letters[0].lower() in "ab", (
            "Punctuation (the ampersands) is skipped; a letter must win")

    def test_no_ampersand_unchanged(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "Settings"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 1
        assert letters[0].lower() in "settings", (
            f"Mnemonic '{letters[0]}' not found in 'Settings'")

    def test_multiple_markers(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "&A,&&B&&C,Settings"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 3
        assert letters[0].lower() in "a", (
            f"First label '&A' should yield 'a', got '{letters[0]}'")
        assert letters[1].lower() in "bc", (
            f"Second label '&&B&&C' should yield 'b' or 'c', got "
            f"'{letters[1]}'")
        assert letters[2].lower() in "setiln", (
            f"Third label 'Settings' should yield a distinct letter, got "
            f"'{letters[2]}'")