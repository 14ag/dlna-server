import subprocess
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
CSS_FILES = [
    "resources/gtk/style.css",
    "resources/gtk/styles2.css",
    "resources/gtk/styles3.css",
    "resources/gtk/styles4.css",
    "resources/gtk/figma.css",
]


@pytest.mark.posix_only
def test_no_duplicate_css_selectors_across_sheets():
    paths = [REPO_ROOT / f for f in CSS_FILES]
    for p in paths:
        assert p.is_file(), f"missing stylesheet {p}"
    combined = subprocess.run(
        ["cat", *[str(p) for p in paths]],
        capture_output=True, text=True, check=True,
    ).stdout
    selectors = []
    for line in combined.splitlines():
        stripped = line.strip()
        if stripped and stripped[0] in ".#" and stripped.endswith("{"):
            selectors.append(stripped[:-1].strip())
    seen = {}
    duplicates = []
    for s in selectors:
        seen[s] = seen.get(s, 0) + 1
    for s, count in seen.items():
        if count > 1:
            duplicates.append((s, count))
    assert duplicates == [], f"duplicate selectors remain: {duplicates}"


def test_border_707070_applied_to_all_windows():
    figma_css = (REPO_ROOT / "resources/gtk/figma.css").read_text(encoding="utf-8")
    assert "707070" in figma_css
    assert "dlna-main-surface" in figma_css
    assert "dlna-dialog-window" in figma_css
