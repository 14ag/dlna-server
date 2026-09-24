import re
import subprocess

import pytest


def _parse_geometry(stdout, tag):
    rows = {}
    pattern = re.compile(
        r"\[gtk4-" + re.escape(tag) + r"-geometry\] class=(\S+) id=(\S+) "
        r"x=(-?\d+) y=(-?\d+) w=(-?\d+) h=(-?\d+)"
    )
    for line in stdout.splitlines():
        m = pattern.search(line)
        if m:
            cls, wid, x, y, w, h = m.groups()
            rows[wid] = {"class": cls, "x": int(x), "y": int(y), "w": int(w), "h": int(h)}
    return rows


@pytest.mark.gui_only
@pytest.mark.needs_xvfb
def test_titlebar_spacing_and_close_button_position(gtk_binary, xvfb_env, tmp_path):
    config_dir = tmp_path / "config"
    config_dir.mkdir()
    env = dict(xvfb_env)
    env["XDG_CONFIG_HOME"] = str(config_dir)
    env["HOME"] = str(config_dir)
    runtime_dir = tmp_path / "runtime"
    runtime_dir.mkdir()
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)

    proc = subprocess.run(
        [gtk_binary, "--dump-widget-geometry"],
        env=env, capture_output=True, text=True, timeout=30,
    )
    rows = _parse_geometry(proc.stdout, "main-window")
    assert "win10-title-icon" in rows, proc.stdout
    assert "win10-title-label" in rows, proc.stdout
    assert "win10-close-button" in rows, proc.stdout

    icon = rows["win10-title-icon"]
    label = rows["win10-title-label"]
    close_button = rows["win10-close-button"]
    window_row = rows.get("0")

    assert icon["x"] == 8, f"icon left edge should be 8px, was {icon['x']}"
    expected_label_x = icon["x"] + icon["w"] + 5
    assert label["x"] == expected_label_x, (
        f"title label should start {expected_label_x}px in "
        f"(icon x + icon w + 5), was {label['x']}"
    )
    if window_row is not None:
        window_right_edge = window_row["x"] + window_row["w"]
        close_right_edge = close_button["x"] + close_button["w"]
        assert abs(window_right_edge - close_right_edge) <= 2, (
            "close button should sit flush with the right edge of the window"
        )
