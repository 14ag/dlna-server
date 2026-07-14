import subprocess

import pytest


def _parse_mnemonics(output):
    result = []
    for token in output.strip().split(","):
        token = token.strip()
        result.append(token if token else "")
    return result


class TestAccessKeyMnemonics:
    def test_unrelated_first_letters(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "Add,Delete,Start,Settings"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 4
        non_empty = [l for l in letters if l]
        assert len(non_empty) >= 2
        for i in range(len(letters)):
            if letters[i]:
                label = ["Add", "Delete", "Start", "Settings"][i]
                assert letters[i].lower() in label.lower(), (
                    f"Mnemonic '{letters[i]}' not found in '{label}'")
        upper_used = set()
        for l in non_empty:
            upper = l.upper()
            assert upper not in upper_used, (
                f"Duplicate mnemonic letter '{l}'")
            upper_used.add(upper)

    def test_logs_help_distinct(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "Logs,Help"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 2
        assert letters[0] and letters[1], (
            "Both Logs and Help should get mnemonics")
        assert letters[0].upper() != letters[1].upper(), (
            "Logs and Help must get different mnemonics")

    def test_single_label(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics", "OK"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 1
        assert letters[0], "Single label 'OK' should get a mnemonic"
        assert letters[0].lower() in "ok"

    def test_exhausted_collision(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-mnemonics",
             "AB,AB,AB"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        letters = _parse_mnemonics(result.stdout)
        assert len(letters) == 3
        non_empty = [l for l in letters if l]
        assert len(non_empty) <= 2, (
            "At most 2 of 3 labels sharing only {A,B} should get a "
            "mnemonic; the third must fall back to empty")
        assert letters[2] == "", (
            "Third label should get no mnemonic (all letters exhausted)")

    def test_tray_menu_variants_both_distinct(self, dlna_binary):
        result_stop = subprocess.run(
            [dlna_binary, "--print-mnemonics",
             "Show Window,Stop Server,Exit"],
            capture_output=True, text=True, timeout=10)
        assert result_stop.returncode == 0, result_stop.stderr
        letters_stop = _parse_mnemonics(result_stop.stdout)
        assert len(letters_stop) == 3
        assert all(l for l in letters_stop), (
            "All three stop-server labels should get mnemonics")

        result_start = subprocess.run(
            [dlna_binary, "--print-mnemonics",
             "Show Window,Start Server,Exit"],
            capture_output=True, text=True, timeout=10)
        assert result_start.returncode == 0, result_start.stderr
        letters_start = _parse_mnemonics(result_start.stdout)
        assert len(letters_start) == 3
        assert all(l for l in letters_start), (
            "All three start-server labels should get mnemonics")
