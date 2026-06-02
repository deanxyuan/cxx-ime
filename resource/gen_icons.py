#!/usr/bin/env python3
"""Generate CxxIME icons using Pillow

Style: raised keycap with 3-side shadow (left, right, bottom) + "文" text + red bamboo badge.
Matches Icon_20's 3D button aesthetic — light from above, shadow on 3 sides.

Produces a 6-resolution ICO file: 256, 64, 48, 32, 24, 16.
"""

import os
import struct
import io
import math
from PIL import Image, ImageDraw, ImageFont

SIZES = [256, 64, 48, 32, 24, 16]

res_dir = os.path.dirname(os.path.abspath(__file__))


def _get_font(size: int) -> ImageFont.FreeTypeFont:
    candidates = ["simkai.ttf", "msyh.ttc", "segoeui.ttf", "arial.ttf"]
    for name in candidates:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def _draw_one_bamboo(draw, cx, cy, r, color, scale=1.0):
    """Draw a single bamboo stalk with two upward leaves."""
    sw = max(1, int(r * 0.06))
    sh = r * 1.4 * scale
    # Stalk
    for i in range(-sw, sw + 1):
        draw.line([(cx + i, cy - sh * 0.7),
                    (cx + i + int(r * 0.05 * scale), cy + sh * 0.6)],
                  fill=color, width=1)
    # Node (joint)
    node_r = max(1, int(r * 0.08))
    draw.ellipse([cx - node_r, cy - node_r, cx + node_r, cy + node_r], fill=color)
    # Leaves
    leaf_len = r * 0.8 * scale
    leaf_w = max(1, int(r * 0.10))
    # Left leaf
    a1 = -math.pi / 3
    lx = cx - r * 0.1 * scale
    ly = cy - r * 0.2 * scale
    for d in range(int(leaf_len)):
        t = d / leaf_len
        wid = int(leaf_w * math.sin(t * math.pi))
        for w in range(-wid, wid + 1):
            px = lx + int(d * math.cos(a1) - w * math.sin(a1))
            py = ly + int(d * math.sin(a1) + w * math.cos(a1))
            draw.point((px, py), fill=color)
    # Right leaf
    a2 = -math.pi / 5
    rx = cx + r * 0.05 * scale
    ry = cy - r * 0.08 * scale
    for d in range(int(leaf_len)):
        t = d / leaf_len
        wid = int(leaf_w * math.sin(t * math.pi))
        for w in range(-wid, wid + 1):
            px = rx + int(d * math.cos(a2) - w * math.sin(a2))
            py = ry + int(d * math.sin(a2) + w * math.cos(a2))
            draw.point((px, py), fill=color)


def _draw_bamboo(draw, cx, cy, r, color):
    """Two bamboo stalks: right-taller, left-shorter."""
    _draw_one_bamboo(draw, cx + int(r * 0.15), cy, r, color, scale=1.0)
    _draw_one_bamboo(draw, cx - int(r * 0.18), cy + int(r * 0.06), r, color, scale=0.65)


def _make_keycap_icon(text: str, size: int, with_badge: bool = True) -> Image.Image:
    """Draw raised keycap with 3-side shadow (left/right/bottom), like Icon_20.

    Light source from above — top edge catches light, left/right/bottom have gray shadow
    creating a 3D extruded button appearance.
    """
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # ── Layout (matching Icon_20 45° viewing angle) ──
    #   canvas edge
    #   → canvas_margin (transparent gap)
    #   → shadow outer edge
    #   → shadow band: gray on left/right/bottom
    #       - left/right: so_side px (~6px at 48)
    #       - bottom:    so_bottom px (~12px at 48, simulating 45° extrusion)
    #   → face edge (flat at top — no top shadow, light from above)
    #   → face interior (white, with text + badge)
    #
    canvas_margin = max(1, size // 48)   # transparent gap to canvas edge
    bottom_margin = max(2, size // 24)   # extra breath below dark line
    so_side = max(2, size // 8)         # left/right shadow width (~6px at 48)
    so_bottom = max(3, size // 5 + 1)   # bottom shadow (~10px at 48, matches Icon_20)
    rad = max(3, size // 10)            # face corner radius (subtle)

    # Shadow rect: inside canvas_margin
    # Bottom uses a larger margin to give the dark line breathing room
    sx0 = canvas_margin
    sy0 = canvas_margin
    sx1 = size - 1 - canvas_margin
    sy1 = size - 1 - bottom_margin

    # Face rect: inset by so_side left/right, so_bottom from bottom, flush at top
    fx0 = sx0 + so_side
    fy0 = sy0                          # flush top — no top shadow
    fx1 = sx1 - so_side
    fy1 = sy1 - so_bottom              # large bottom gap for 45° extrusion

    # ── Shadow — sampled from Icon_20 pixel profile ──
    # Profile (distance from face edge, row 24):
    #   dist=2+:  RGB(204,203,203)  uniform shadow body
    #   dist=1:   RGB(213,212,212)  thin highlight band (lighter!)
    #   bottom:   RGB(0,0,0)        dark contact-shadow line
    shadow_rad = rad + so_side
    shadow_body = (204, 203, 203)

    # Shadow body — uniform fill, fully opaque (matches Icon_20 alpha=255)
    draw.rounded_rectangle(
        [sx0, sy0, sx1, sy1],
        radius=shadow_rad,
        fill=(*shadow_body, 255))

    # 1px dark contact-shadow line at the BOTTOM edge only (fully opaque)
    bottom_y = sy1
    left_x = sx0 + shadow_rad
    right_x = sx1 - shadow_rad
    if right_x > left_x:
        draw.line([(left_x, bottom_y), (right_x, bottom_y)],
                  fill=(0, 0, 0, 255), width=1)

    # Face fill — warm white
    draw.rounded_rectangle([fx0, fy0, fx1, fy1], radius=rad, fill=(252, 251, 250, 255))

    # ── Text: dark, centered on face ──
    # Draw on a separate layer then composite — prevents the glyph bounding box
    # from overwriting face pixels with transparency.
    font_ratio = 0.46 if with_badge else 0.52
    font_size = max(8, int(size * font_ratio))
    font = _get_font(font_size)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    fcx = (fx0 + fx1) // 2
    fcy = (fy0 + fy1) // 2
    tx = fcx - tw // 2
    ty = fcy - th // 2 - bbox[1]

    # Badge now straddles the face-shadow corner — no need to offset text
    text_color = (50, 48, 52)
    text_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    text_draw = ImageDraw.Draw(text_layer)
    text_draw.text((tx, ty), text, fill=(*text_color, 255), font=font)
    img = Image.alpha_composite(img, text_layer)
    draw = ImageDraw.Draw(img)  # refresh draw after composite

    # ── Red badge with bamboo — straddles face/shadow boundary ──
    # Sized to match Icon_20 paw pad prominence (~250px area at 48x48).
    # Positioned so right/bottom edges align with shadow outer edge.
    if with_badge:
        badge_r = max(5, size // 5)  # ~9px at 48x48, ~250px area
        badge_cx = sx1 - badge_r     # right edge flush with shadow outer edge
        badge_cy = sy1 - badge_r     # bottom edge flush with shadow outer edge (above dark line)

        # Badge fill — flat red
        draw.ellipse(
            [badge_cx - badge_r, badge_cy - badge_r,
             badge_cx + badge_r, badge_cy + badge_r],
            fill=(225, 55, 55, 255))

        # Bamboo inside badge
        bamboo_r = int(badge_r * 0.55)
        _draw_bamboo(draw, badge_cx, badge_cy, bamboo_r, (255, 255, 255, 255))

    return img


def _make_keycap_icon_small(text: str, size: int) -> Image.Image:
    """Simplified raised keycap for sizes 16 and 24 — fewer details."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    margin = max(1, size // 12)
    so = max(1, size // 12)
    rad = max(3, size // 5)

    # Shadow (wider + lower)
    draw.rounded_rectangle(
        [margin - so, margin, size - 1 - margin + so, size - 1 - margin + so],
        radius=rad + so, fill=(204, 203, 203, 255))

    # Face (slightly up)
    face_up = max(0, size // 24)
    fx0 = margin
    fy0 = margin - face_up
    fx1 = size - 1 - margin
    fy1 = size - 1 - margin - face_up

    draw.rounded_rectangle([fx0, fy0, fx1, fy1], radius=rad, fill=(252, 251, 250, 255))

    # Text
    font_size = max(6, int(size * 0.58))
    font = _get_font(font_size)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    fcx = (fx0 + fx1) // 2
    fcy = (fy0 + fy1) // 2
    tx = fcx - tw // 2
    ty = fcy - th // 2 - bbox[1]
    draw.text((tx, ty), text, fill=(50, 48, 52, 255), font=font)

    return img


def _write_ico(images: list, filename: str):
    """Write a multi-resolution .ico file (PNG-encoded frames)."""
    png_list = []
    for img in images:
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        png_list.append(buf.getvalue())

    data = bytearray()
    count = len(images)
    data += struct.pack("<HHH", 0, 1, count)

    header_size = 6 + count * 16
    for idx, png_bytes in enumerate(png_list):
        w = images[idx].width
        h = images[idx].height
        stored_w = 0 if w >= 256 else w
        stored_h = 0 if h >= 256 else h
        png_size = len(png_bytes)
        png_offset = header_size + sum(len(p) for p in png_list[:idx])
        data += struct.pack("<BBBBHHII", stored_w, stored_h, 0, 0, 1, 32, png_size, png_offset)

    for png_bytes in png_list:
        data += png_bytes

    with open(filename, "wb") as f:
        f.write(data)


def create_icon(text: str, filename: str, with_badge: bool = True):
    """Create a multi-resolution .ico file."""
    images = []
    for s in SIZES:
        if s <= 24:
            images.append(_make_keycap_icon_small(text, s))
        else:
            images.append(_make_keycap_icon(text, s, with_badge))
    _write_ico(images, filename)
    print(f"Created: {os.path.basename(filename)}  ({', '.join(f'{s}x{s}' for s in SIZES)})")


# ── Generate all icons ──────────────────────────────────────────────

if __name__ == "__main__":
    # cxxime.ico — "文" with bamboo badge
    create_icon("文", os.path.join(res_dir, "cxxime.ico"), with_badge=True)

    # Status icons — keycap, no badge
    create_icon("ZH", os.path.join(res_dir, "zh.ico"), with_badge=False)
    create_icon("EN", os.path.join(res_dir, "en.ico"), with_badge=False)
    create_icon("F",  os.path.join(res_dir, "full.ico"), with_badge=False)
    create_icon("H",  os.path.join(res_dir, "half.ico"), with_badge=False)

    print("All icons generated!")
