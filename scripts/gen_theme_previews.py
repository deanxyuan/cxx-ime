# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.
"""Render candidate window theme previews for the README.

Replicates the layout and rendering logic of the ui module
(ui/src/layout.cc, ui/src/gdi_renderer.cc, ui/src/theme.cc,
ui/src/candidate_window.cc) using Pillow, so the previews match what the
real candidate window looks like:

- horizontal layout formulas (label + spacing + text, highlight inflation)
- preedit line with cursor and separator
- rounded highlight box on the first candidate
- page nav buttons (previous dimmed, next enabled)
- rounded window region and 1px border

Output: one PNG per preset color scheme in docs/images/themes/.
Usage: python scripts/gen_theme_previews.py [--scale 2] [--output docs/images/themes]
"""

import argparse
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def bgr_to_rgb(value: int) -> tuple:
    """themes.json stores colors as BGR integers (matching the C++ side)."""
    return (value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF)


def blend(a: tuple, b: tuple, weight: float) -> tuple:
    return tuple(
        int(round(a[i] * (1.0 - weight) + b[i] * weight)) for i in range(3)
    )


def relative_luminance(rgb: tuple) -> float:
    def linear(channel: int) -> float:
        value = channel / 255.0
        if value <= 0.04045:
            return value / 12.92
        return ((value + 0.055) / 1.055) ** 2.4

    return 0.2126 * linear(rgb[0]) + 0.7152 * linear(rgb[1]) + 0.0722 * linear(rgb[2])


def contrast_ratio(a: tuple, b: tuple) -> float:
    lighter = max(relative_luminance(a), relative_luminance(b))
    darker = min(relative_luminance(a), relative_luminance(b))
    return (lighter + 0.05) / (darker + 0.05)


def color_distance(a: tuple, b: tuple) -> int:
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


def default_preedit_cursor_color(colors: dict) -> tuple:
    """Mirror of engine/src/config.cc default_preedit_cursor_color()."""
    back = bgr_to_rgb(colors["back_color"])
    hilited_text = bgr_to_rgb(colors["hilited_text_color"])
    adjustment_target = (
        (0, 0, 0)
        if contrast_ratio((0, 0, 0), back) > contrast_ratio((255, 255, 255), back)
        else (255, 255, 255)
    )
    blend_amounts = (0.0, 0.12, 0.24, 0.36, 0.48, 0.60, 0.72, 0.84)
    sample_keys = (
        "hilited_candidate_back_color",
        "hilited_back_color",
        "border_color",
        "label_text_color",
        "candidate_text_color",
        "comment_text_color",
        "text_color",
        "hilited_candidate_text_color",
    )
    for key in sample_keys:
        sample = bgr_to_rgb(colors[key])
        for amount in blend_amounts:
            candidate = blend(sample, adjustment_target, amount)
            if (
                contrast_ratio(candidate, back) >= 4.5
                and color_distance(candidate, hilited_text) >= 96
            ):
                return candidate

    light_background = relative_luminance(back) >= 0.5
    preferred = (0x67, 0xC0, 0x00) if light_background else (0xFF, 0xD2, 0x3F)
    alternate = (0x26, 0xA4, 0x2C) if light_background else (0xC2, 0x4C, 0xFF)
    if color_distance(preferred, hilited_text) >= 96:
        return preferred
    return alternate


def load_font(path_candidates, size):
    for path in path_candidates:
        if os.path.isfile(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                continue
    return ImageFont.load_default()


def font_height(font) -> int:
    ascent, descent = font.getmetrics()
    return ascent + descent


def render_preview(scheme: dict, config: dict, scale: int) -> Image.Image:
    style = config["style"]
    layout = config["layout"]

    s = scale
    margin_x = layout["margin_x"] * s
    margin_y = layout["margin_y"] * s
    spacing = layout["spacing"] * s
    candidate_spacing = layout["candidate_spacing"] * s
    hilite_spacing = layout["hilite_spacing"] * s
    hilite_padding_x = layout["hilite_padding_x"] * s
    hilite_padding_y = layout["hilite_padding_y"] * s
    round_corner = layout["round_corner"] * s
    round_corner_ex = layout["round_corner_ex"] * s
    border_width = layout["border_width"] * s

    font_name = style["font_face"]
    font_size = style["font_point"]

    # Font pixel heights, mirroring CreateFontW(-MulDiv(size, dpi, 72), ...).
    dpi = 96 * s
    main_size = max(1, int(round(font_size * dpi / 72)))
    preedit_size = max(1, int(round((font_size - 2) * dpi / 72)))
    nav_size = max(1, int(round(9 * dpi / 72)))

    fonts_dir = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
    font_candidates = [
        os.path.join(fonts_dir, "msyh.ttc"),
        os.path.join(fonts_dir, "msyh.ttf"),
        os.path.join(fonts_dir, "msyhl.ttc"),
    ]
    main_font = load_font(font_candidates, main_size)
    preedit_font = load_font(font_candidates, preedit_size)
    nav_font = load_font(font_candidates, nav_size)

    theme = {
        "background": bgr_to_rgb(scheme["back_color"]),
        "border": bgr_to_rgb(scheme["border_color"]),
        "text": bgr_to_rgb(scheme["candidate_text_color"]),
        "comment_text": bgr_to_rgb(scheme["comment_text_color"]),
        "label_text": bgr_to_rgb(scheme["label_text_color"]),
        "hilited_text": bgr_to_rgb(scheme["hilited_candidate_text_color"]),
        "hilited_back": bgr_to_rgb(scheme["hilited_candidate_back_color"]),
        "preedit_text": bgr_to_rgb(scheme["hilited_text_color"]),
        "preedit_cursor": (
            bgr_to_rgb(scheme["preedit_cursor_color"])
            if scheme.get("preedit_cursor_color", -1) != -1
            else default_preedit_cursor_color(scheme)
        ),
        "prev_page": bgr_to_rgb(scheme["prevpage_color"]),
        "next_page": bgr_to_rgb(scheme["nextpage_color"]),
    }
    separator_color = blend(theme["background"], theme["text"], 0.25)

    candidates = ["你好", "您好", "昵称", "尼采", "拟态", "腻烦", "匿藏"]
    preedit = "ni'hao"

    rh = font_height(main_font)
    text_slack = max(2, rh // 4)

    # --- horizontal layout (mirror of calculate_horizontal_layout) ---
    rects = []
    x = margin_x
    y = margin_y
    for index, text in enumerate(candidates):
        label = f"{index + 1}."
        label_w = math.ceil(main_font.getlength(label))
        text_w = math.ceil(main_font.getlength(text)) + text_slack
        total_w = label_w + hilite_spacing + text_w
        label_rect = (x, y, x + label_w, y + rh)
        text_left = x + label_w + hilite_spacing
        text_rect = (text_left, y, text_left + text_w, y + rh)
        bounds = (label_rect[0], y, text_rect[2], y + rh)
        highlight_rect = (
            bounds[0] - hilite_padding_x,
            bounds[1] - hilite_padding_y,
            bounds[2] + hilite_padding_x,
            bounds[3] + hilite_padding_y,
        )
        rects.append(
            {
                "index": index,
                "text": text,
                "label": label,
                "label_rect": label_rect,
                "text_rect": text_rect,
                "highlight_rect": highlight_rect,
            }
        )
        x += total_w + candidate_spacing

    content_w = rects[-1]["text_rect"][2] + candidate_spacing
    width = content_w + margin_x

    # --- preedit row (mirror of CandidateWindow::update) ---
    ps_cy = font_height(preedit_font)
    preedit_h = ps_cy + spacing
    for rect in rects:
        for key in ("label_rect", "text_rect", "highlight_rect"):
            rect[key] = (
                rect[key][0],
                rect[key][1] + preedit_h,
                rect[key][2],
                rect[key][3] + preedit_h,
            )
    preedit_rect = (margin_x, margin_y, width - margin_x, margin_y + ps_cy)
    row_height = rh
    height = row_height + margin_y * 2 + preedit_h

    # --- page nav buttons (page 1 of 3: previous dimmed, next enabled) ---
    last = rects[-1]
    nav_h = last["highlight_rect"][3] - last["highlight_rect"][1]
    nav_y = last["highlight_rect"][1]
    nav_x = last["highlight_rect"][2] + 4 * s
    prev_button = (nav_x, nav_y, nav_x + 16 * s, nav_y + nav_h)
    next_button = (nav_x + 18 * s, nav_y, nav_x + 34 * s, nav_y + nav_h)
    if next_button[2] > width:
        width = next_button[2] + margin_x

    # --- window sizing: add border, then draw ---
    canvas_w = width + border_width * 2
    canvas_h = height + border_width * 2
    img = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Rounded window background (window region uses round_corner_ex).
    draw.rounded_rectangle(
        (0, 0, canvas_w - 1, canvas_h - 1),
        radius=round_corner_ex,
        fill=theme["background"],
    )

    # Preedit line with cursor at the end.
    preedit_cy = preedit_rect[1] + (preedit_rect[3] - preedit_rect[1]) / 2
    draw.text((preedit_rect[0], preedit_cy), preedit, font=preedit_font, fill=theme["preedit_text"], anchor="lm")
    prefix_w = math.ceil(preedit_font.getlength(preedit))
    cursor_left = preedit_rect[0] + prefix_w
    cursor_left = max(preedit_rect[0], min(cursor_left, preedit_rect[2] - 1))
    draw.rectangle(
        (cursor_left, preedit_rect[1] + 1, cursor_left + 1, preedit_rect[3] - 1),
        fill=theme["preedit_cursor"],
    )
    sep_y = preedit_rect[3] + spacing // 3
    draw.line(
        (margin_x + 2, sep_y, canvas_w - margin_x - 2, sep_y),
        fill=separator_color,
        width=1,
    )

    # Candidates.
    for rect in rects:
        highlighted = rect["index"] == 0
        if highlighted:
            draw.rounded_rectangle(
                rect["highlight_rect"],
                radius=round_corner,
                fill=theme["hilited_back"],
            )
        label_color = theme["hilited_text"] if highlighted else theme["label_text"]
        text_color = theme["hilited_text"] if highlighted else theme["text"]
        for text, box, color in (
            (rect["label"], rect["label_rect"], label_color),
            (rect["text"], rect["text_rect"], text_color),
        ):
            cy = box[1] + (box[3] - box[1]) / 2
            draw.text((box[0], cy), text, font=main_font, fill=color, anchor="lm")

    # Page nav arrows: "<" dimmed (disabled), ">" enabled.
    dim_color = separator_color

    def draw_arrow(box, color, glyph):
        cy = box[1] + (box[3] - box[1]) / 2
        draw.text((box[0] + (box[2] - box[0]) / 2, cy), glyph, font=nav_font, fill=color, anchor="mm")

    draw_arrow(prev_button, dim_color, "<")
    draw_arrow(next_button, theme["prev_page"], ">")

    # 1px border (round_corner_ex radius), inset by half the border width.
    inset = border_width / 2
    draw.rounded_rectangle(
        (inset, inset, canvas_w - inset, canvas_h - inset),
        radius=round_corner_ex,
        outline=theme["border"],
        width=border_width,
    )
    return img


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scale", type=int, default=2, help="render scale (default 2)")
    parser.add_argument(
        "--output",
        default=None,
        help="output directory (default: <repo>/docs/images/themes)",
    )
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(repo_root, "data", "themes.json"), encoding="utf-8") as f:
        themes = json.load(f)["preset_color_schemes"]
    with open(os.path.join(repo_root, "data", "default.json"), encoding="utf-8") as f:
        config = json.load(f)

    output_dir = args.output or os.path.join(repo_root, "docs", "images", "themes")
    os.makedirs(output_dir, exist_ok=True)
    for name, scheme in themes.items():
        image = render_preview(scheme, config, args.scale)
        path = os.path.join(output_dir, f"{name}.png")
        image.save(path)
        print(f"wrote {path} ({image.width}x{image.height})")


if __name__ == "__main__":
    sys.exit(main())
