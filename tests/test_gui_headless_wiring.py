from pathlib import Path

import pytest

pytestmark = pytest.mark.posix_only

SRC = (Path(__file__).resolve().parents[1] / "src" / "gtk4_gui_main.cpp").read_text()


def test_headless_decision_uses_shared_policy():
    assert "ShouldStartHeadless(explicitHeadless, !runtimeSources.empty())" in SRC


def test_headless_skips_present_and_starts_server():
    assert "if (g_startHeadless) {" in SRC
