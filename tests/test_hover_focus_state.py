import subprocess

import pytest


def run_hover_focus_state(binary_path, sequence):
    result = subprocess.run(
        [binary_path, "--print-hover-focus-state", sequence],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0, result.stderr
    lines = [line for line in result.stdout.strip().splitlines() if line]
    return [int(line) for line in lines]


class TestHoverFocusState:
    def test_hover_enter_shows_highlight(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "e1") == [1]

    def test_hover_leave_clears_highlight(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "e1,l1") == [1, -1]

    def test_hover_leave_then_reenter_restores_highlight(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "e1,l1,e1") == [1, -1, 1]
        assert run_hover_focus_state(dlna_binary, "e1,l1,e1,l1,e1") == [1, -1, 1, -1, 1]

    def test_keyboard_focus_shows_highlight_without_hover(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "f2") == [2]

    def test_keyboard_focus_lost_clears_highlight(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "f2,b2") == [2, -1]

    def test_hover_wins_over_existing_keyboard_focus(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "f2,e1") == [2, 1]

    def test_focus_indicator_returns_after_hover_leaves(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "f2,e1,l1") == [2, 1, 2]

    def test_unrelated_leave_does_not_clear_different_control(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "e1,l3") == [1, 1]

    def test_unrelated_blur_does_not_clear_different_control(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "f2,b3") == [2, 2]

    def test_empty_sequence_produces_no_output(self, dlna_binary):
        assert run_hover_focus_state(dlna_binary, "") == []

    @pytest.mark.parametrize("control_id", [0, 1, 4, 205])
    def test_arbitrary_control_ids_round_trip(self, dlna_binary, control_id):
        assert run_hover_focus_state(dlna_binary, f"e{control_id}") == [control_id]
