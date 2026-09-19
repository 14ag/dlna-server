import re


def _const(text, name):
    match = re.search(rf"(?:constexpr int\s+)?{name}\s*=\s*(\d+)", text)
    assert match, f"{name} not found"
    return int(match.group(1))


def test_posix_toolbar_height_matches_win32(repo_root):
    win = (repo_root / "src" / "ui_tokens.h").read_text()
    posix = (repo_root / "src" / "ui_tokens_posix.h").read_text()
    figma = (repo_root / "resources" / "gtk" / "figma.css").read_text()

    win_height = _const(win, "kToolbarHeight")
    posix_height = _const(posix, "kMainToolbarHeight")
    assert posix_height == win_height

    # GTK takes max(size_request, CSS min-height); both must agree or the
    # rendered toolbar silently grows.
    match = re.search(r"box\.toolbar\s*\{[^}]*min-height:\s*(\d+)px", figma, re.S)
    assert match, "box.toolbar min-height not found in figma.css"
    assert int(match.group(1)) == win_height


def test_posix_toolbar_buttons_are_vertically_centred(repo_root):
    win = (repo_root / "src" / "ui_tokens.h").read_text()
    posix = (repo_root / "src" / "ui_tokens_posix.h").read_text()
    expected = (_const(posix, "kMainToolbarHeight") - _const(posix, "kAddButtonH")) // 2
    for name in ("kAddButtonY", "kDeleteButtonY", "kStartStopButtonY", "kSettingsButtonY"):
        match = re.search(rf"{name}\s*=\s*(\d+)", posix)
        assert match, f"{name} not found"
        assert int(match.group(1)) == expected
