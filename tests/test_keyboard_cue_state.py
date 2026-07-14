import subprocess

import pytest


class TestKeyboardCueState:
    def test_initial_state_hidden(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-cue-state", "x"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 1
        assert lines[0] == "1,1", (
            "Initial state should hide both accel and focus")

    def test_keyboard_reveals(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-cue-state", "k"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 1
        assert lines[0] == "0,0", (
            "Keyboard input should reveal both accel and focus")

    def test_mouse_hides(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-cue-state", "m"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 1
        assert lines[0] == "1,1", (
            "Mouse input should hide both accel and focus")

    def test_keyboard_then_mouse(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-cue-state", "km"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 2
        assert lines[0] == "0,0", (
            "After keyboard: both revealed")
        assert lines[1] == "1,1", (
            "After mouse: both hidden again")

    def test_mouse_then_keyboard(self, dlna_binary):
        result = subprocess.run(
            [dlna_binary, "--print-cue-state", "mk"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        lines = result.stdout.strip().splitlines()
        assert len(lines) == 2
        assert lines[0] == "1,1", (
            "After mouse: both hidden")
        assert lines[1] == "0,0", (
            "After keyboard: both revealed")
