#!/usr/bin/env python3
"""Generate resources/gtk/style.css from src/ui_tokens.h.

Dependency-free Python. Regex-extracts every constexpr int / RgbColor /
const char* from the token header and renders a fixed CSS template. Every
token referenced by the template must exist in the header, otherwise the
script exits non-zero and names the missing token, so a rename in one file
cannot silently desync the other.
"""

import os
import re
import sys

INT_RE = re.compile(r"constexpr\s+int\s+k([A-Za-z0-9_]+)\s*=\s*(\d+)\s*;")
COLOR_RE = re.compile(
    r"constexpr\s+RgbColor\s+k([A-Za-z0-9_]+)\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}\s*;"
)
STRING_RE = re.compile(
    r'constexpr\s+const\s+char\*\s+k([A-Za-z0-9_]+)\s*=\s*"([^"]*)"\s*;'
)
PLACEHOLDER_RE = re.compile(r"\{\{(\w+)\}\}")


def camel_to_snake(name):
    name = re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()
    return name


def token_name(identifier):
    if identifier.startswith("k") and len(identifier) > 1 and identifier[1].isupper():
        identifier = identifier[1:]
    return camel_to_snake(identifier)


def parse_header(text):
    tokens = {}
    for match in INT_RE.finditer(text):
        tokens[token_name(match.group(1))] = ("int", int(match.group(2)))
    for match in COLOR_RE.finditer(text):
        tokens[token_name(match.group(1))] = (
            "color",
            (int(match.group(2)), int(match.group(3)), int(match.group(4))),
        )
    for match in STRING_RE.finditer(text):
        tokens[token_name(match.group(1))] = ("string", match.group(2))
    return tokens


CSS_TEMPLATE = """/* auto generated from src/ui_tokens.h by tools/gen_gtk_style.py */
/* do not hand edit this file */
/* rerun tools/gen_gtk_style.py after changing ui_tokens.h */

@define-color page_color {{page_color}};
@define-color toolbar_color {{toolbar_color}};
@define-color control_color {{control_color}};
@define-color control_hover_color {{control_hover_color}};
@define-color control_pressed_color {{control_pressed_color}};
@define-color border_color {{border_color}};
@define-color focus_color {{focus_color}};
@define-color text_color {{text_color}};
@define-color disabled_text_color {{disabled_text_color}};
@define-color secondary_text_color {{secondary_text_color}};

window.dlna-main {
    background-color: @page_color;
    color: @text_color;
}

.toolbar {
    background-color: @toolbar_color;
    min-height: {{toolbar_height}}px;
}

.window-title {
    font-family: "{{title_font_family_stack}}";
    font-size: {{title_font_size_px}}px;
    font-weight: 600;
    color: @text_color;
}

.status-band {
    background-color: @page_color;
    color: @text_color;
    min-height: {{status_height}}px;
    padding: 0 {{gutter}}px;
}

    .toolbar-button {
        background-color: @control_color;
        border: none;
        border-radius: {{corner_radius}}px;
        color: @text_color;
        min-height: {{button_height}}px;
        min-width: {{add_button_width}}px;
        outline: 1px solid @border_color;
        outline-offset: -1px;
        padding: 0;
        font-family: "{{body_font_family_stack}}";
        font-size: 12px;
    }

.toolbar-button:hover {
    background-color: @control_hover_color;
}

.toolbar-button:active {
    background-color: @control_pressed_color;
}

.toolbar-button:disabled {
    background-color: @control_color;
    color: @disabled_text_color;
}

.toolbar-button:focus-visible {
    outline: {{focus_ring_thickness}}px solid @focus_color;
    outline-offset: -4px;
}

.source-list:focus-within {
    outline: {{focus_ring_thickness}}px solid @focus_color;
    outline-offset: {{focus_ring_gap}}px;
}

.empty-state {
    color: @secondary_text_color;
}
"""


def render(template, tokens):
    referenced = set(PLACEHOLDER_RE.findall(template))
    missing = []
    for name in sorted(referenced):
        if name not in tokens:
            missing.append(name)
    if missing:
        message = (
            "gen_gtk_style.py: token(s) referenced by the CSS template are "
            "missing from ui_tokens.h: " + ", ".join(missing)
        )
        print(message, file=sys.stderr)
        sys.exit(1)

    def resolve(match):
        name = match.group(1)
        kind, value = tokens[name]
        if kind == "int":
            if name == "button_height":
                # GTK4 buttons add a 1px border top+bottom (2px total) so
                # the CSS min-height must be 2 less than the token to produce
                # the exact button height expected by the geometry tests.
                return str(value - 2)
            return str(value)
        if kind == "color":
            return "rgb({},{},{})".format(value[0], value[1], value[2])
        return value

    return PLACEHOLDER_RE.sub(resolve, template)


def default_paths():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    header = os.path.join(repo_root, "src", "ui_tokens.h")
    output = os.path.join(repo_root, "resources", "gtk", "style.css")
    return header, output


def main(argv):
    if len(argv) > 2:
        print("usage: gen_gtk_style.py [ui_tokens.h] [style.css]", file=sys.stderr)
        sys.exit(2)
    header_path, output_path = default_paths()
    if len(argv) >= 1:
        header_path = argv[0]
    if len(argv) >= 2:
        output_path = argv[1]

    with open(header_path, "r", encoding="utf-8") as handle:
        header_text = handle.read()

    tokens = parse_header(header_text)
    css = render(CSS_TEMPLATE, tokens)

    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as handle:
        handle.write(css)

    print("gen_gtk_style.py: wrote " + output_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
