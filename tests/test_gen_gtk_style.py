import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools" / "gen_gtk_style.py"

FIXTURE_HEADER = """
#ifndef UI_TOKENS_H
#define UI_TOKENS_H

struct RgbColor {
    int r;
    int g;
    int b;
};

namespace UiTokens {

constexpr int kWindowWidth = 440;
constexpr int kWindowHeight = 600;
constexpr int kToolbarHeight = 56;
constexpr int kStatusHeight = 40;
constexpr int kListTopGap = 8;
constexpr int kButtonHeight = 32;
constexpr int kButtonGap = 8;
constexpr int kGutter = 16;
constexpr int kCornerRadius = 8;
constexpr int kFocusRingThickness = 1;
constexpr int kFocusRingGap = 2;

constexpr RgbColor kPageColor = { 31, 31, 31 };
constexpr RgbColor kToolbarColor = { 37, 37, 37 };
constexpr RgbColor kControlColor = { 51, 51, 51 };
constexpr RgbColor kControlHoverColor = { 62, 62, 62 };
constexpr RgbColor kControlPressedColor = { 74, 74, 74 };
constexpr RgbColor kBorderColor = { 88, 88, 88 };
constexpr RgbColor kFocusColor = { 96, 165, 250 };
constexpr RgbColor kTextColor = { 255, 255, 255 };
constexpr RgbColor kDisabledTextColor = { 132, 132, 132 };
constexpr RgbColor kSecondaryTextColor = { 200, 200, 200 };

constexpr int kTitleFontSizePx = 20;
constexpr int kBodyFontSizePx = 14;
constexpr const char* kTitleFontFamilyStack = "Segoe UI Variable Display";
constexpr const char* kBodyFontFamilyStack = "Segoe UI Variable Text";

constexpr int kAddButtonWidth = 56;

constexpr int kWin10TitlebarHeight = 32;
constexpr RgbColor kWin10InactiveTitlebarColor = { 50, 50, 50 };

}  // namespace UiTokens

#endif  // UI_TOKENS_H
"""


def run_generator(tmp_path, header_text):
    header = tmp_path / "ui_tokens.h"
    header.write_text(header_text, encoding="utf-8")
    output = tmp_path / "style.css"
    result = subprocess.run(
        [sys.executable, str(TOOLS), str(header), str(output)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    return result, output


def test_emits_define_color_lines(tmp_path):
    result, output = run_generator(tmp_path, FIXTURE_HEADER)
    assert result.returncode == 0, result.stderr
    css = output.read_text(encoding="utf-8")
    assert "@define-color page_color rgb(31,31,31);" in css
    assert "@define-color toolbar_color rgb(37,37,37);" in css
    assert "@define-color control_hover_color rgb(62,62,62);" in css
    assert "@define-color focus_color rgb(96,165,250);" in css
    assert "@define-color secondary_text_color rgb(200,200,200);" in css


def test_inlines_px_values(tmp_path):
    result, output = run_generator(tmp_path, FIXTURE_HEADER)
    assert result.returncode == 0, result.stderr
    css = output.read_text(encoding="utf-8")
    assert "min-height: 56px;" in css
    assert "font-size: 20px;" in css
    assert "border-radius: 8px;" in css
    assert "min-width: 56px;" in css


def test_inlines_font_family_strings(tmp_path):
    result, output = run_generator(tmp_path, FIXTURE_HEADER)
    assert result.returncode == 0, result.stderr
    css = output.read_text(encoding="utf-8")
    assert '"Segoe UI Variable Display"' in css
    assert '"Segoe UI Variable Text"' in css


def test_exits_nonzero_when_token_missing(tmp_path):
    broken = FIXTURE_HEADER.replace("constexpr int kToolbarHeight = 56;", "")
    result, _ = run_generator(tmp_path, broken)
    assert result.returncode != 0
    assert "kToolbarHeight" in result.stderr or "toolbar_height" in result.stderr


def test_no_output_file_on_missing_token(tmp_path):
    broken = FIXTURE_HEADER.replace("constexpr RgbColor kPageColor = { 31, 31, 31 };", "")
    result, output = run_generator(tmp_path, broken)
    assert result.returncode != 0
    assert not output.exists()
