#!/usr/bin/env python3
"""
Icon preview v5 — Structurally correct bamboo:
- Main stalk nodes → multi-segment branches (≥2 internodes with rings)
- Slender lanceolate leaves (6:1+ aspect ratio)
- Leaves only at branch tips, never directly from main stalk nodes
- Classic upward fan: 2-4 leaves per branch tip
"""

import os
import io
import struct
import math
from PIL import Image, ImageDraw

RES_DIR = os.path.dirname(os.path.abspath(__file__))
SIZES = [256, 64, 48, 32, 24, 16]


def _write_ico(images, filepath):
    """Write multi-resolution .ico with PNG-encoded frames."""
    pngs = []
    for img in images:
        buf = io.BytesIO()
        img.save(buf, format="PNG")
        pngs.append(buf.getvalue())
    data = bytearray()
    data += struct.pack("<HHH", 0, 1, len(images))
    hdr = 6 + len(images) * 16
    for i, p in enumerate(pngs):
        w, h = images[i].width, images[i].height
        sw = 0 if w >= 256 else w
        sh = 0 if h >= 256 else h
        off = hdr + sum(len(pngs[j]) for j in range(i))
        data += struct.pack("<BBBBHHII", sw, sh, 0, 0, 1, 32, len(p), off)
    for p in pngs:
        data += p
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, "wb") as f:
        f.write(data)


def _circle_bbox(cx, cy, r):
    return [cx - r, cy - r, cx + r, cy + r]


def draw_slender_brush_leaf(draw, base_x, base_y, tip_x, tip_y, max_width, color):
    """Slender bamboo leaf: length/width ≈ 6:1 to 8:1.

    Curved crescent profile, widest at ~1/4 from base, sharp tip.
    Base is always BELOW tip (leaf points upward).
    """
    dx = tip_x - base_x
    dy = tip_y - base_y
    length = math.hypot(dx, dy)
    if length < 2:
        return

    ux, uy = dx / length, dy / length
    px, py = -uy, ux

    bow = length * 0.12  # subtle arch

    steps = 40
    left = []
    right = []

    for i in range(steps + 1):
        t = i / steps
        cx = base_x + dx * t
        cy = base_y + dy * t
        bow_off = bow * math.sin(t * math.pi) ** 0.6

        # Width: quick press at base, long gradual taper
        if t < 0.04:
            w = max_width * (t / 0.04) * 0.2
        elif t < 0.22:
            frac = (t - 0.04) / 0.18
            w = max_width * (0.2 + 0.8 * frac)
        else:
            frac = (t - 0.22) / 0.78
            w = max_width * (1.0 - frac) ** 0.5
        w = max(0.2, w)

        left.append((cx + px * (w + bow_off), cy + py * (w + bow_off)))
        right.insert(0, (cx - px * (w - bow_off * 0.4), cy - py * (w - bow_off * 0.4)))

    outline = left + right
    if len(outline) >= 3:
        draw.polygon(outline, fill=color)

        # Midrib for large leaves
        if length > 12:
            mid_c = tuple(min(255, c + (40 if c < 128 else -30)) for c in color[:3]) + (color[3],)
            prev = None
            for j in range(18):
                t = j / 17
                cx = base_x + dx * t
                cy = base_y + dy * t
                bo = bow * math.sin(t * math.pi) ** 0.6 * 0.35
                pt = (cx + px * bo, cy + py * bo)
                if prev:
                    draw.line([prev, pt], fill=mid_c, width=1)
                prev = pt


def draw_segment(draw, x0, y0, x1, y1, w0, w1, color):
    """Draw a single internode segment (hollow tube between rings)."""
    dx = x1 - x0
    dy = y1 - y0
    length = math.hypot(dx, dy)
    if length < 0.5:
        return
    ux, uy = dx / length, dy / length
    px, py = -uy, ux

    if w0 > 0.3 and w1 > 0.3:
        corners = [
            (x0 + px * w0 / 2, y0 + py * w0 / 2),
            (x1 + px * w1 / 2, y1 + py * w1 / 2),
            (x1 - px * w1 / 2, y1 - py * w1 / 2),
            (x0 - px * w0 / 2, y0 - py * w0 / 2),
        ]
        draw.polygon(corners, fill=color)
        # Subtle highlight
        hl = tuple(min(255, c + 30) for c in color[:3]) + (color[3],)
        hl_w = max(1, int(w0 * 0.2))
        draw.line([(x0 + px * w0 / 5, y0 + py * w0 / 5),
                    (x1 + px * w1 / 5, y1 + py * w1 / 5)],
                  fill=hl, width=hl_w)


def draw_node_ring(draw, x, y, ux, uy, px, py, stalk_w, color):
    """Prominent node ring: wider than stalk, with highlight edge."""
    nrw = stalk_w * 1.8
    nrh = max(1.0, stalk_w * 0.55)
    rc = [
        (x + px * nrw / 2, y + py * nrw / 2),
        (x + px * nrw / 2 + ux * nrh, y + py * nrw / 2 + uy * nrh),
        (x - px * nrw / 2 + ux * nrh, y - py * nrw / 2 + uy * nrh),
        (x - px * nrw / 2, y - py * nrw / 2),
    ]
    draw.polygon(rc, fill=color)
    bright = tuple(min(255, c + 60) for c in color[:3]) + (color[3],)
    draw.line([(x + px * nrw / 2, y + py * nrw / 2),
                (x - px * nrw / 2, y - py * nrw / 2)],
              fill=bright, width=max(1, int(nrh * 0.3)))


def draw_stalk_with_nodes(draw, x0, y0, x1, y1, width, color, num_nodes,
                          taper=0.15):
    """Draw a stalk (main or branch) with segments and node rings.

    Returns list of (node_x, node_y, ux, uy, px, py, stalk_w_at_node)
    for nodes that can grow branches.
    """
    dx = x1 - x0
    dy = y1 - y0
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    px, py = -uy, ux

    node_info = []

    for i in range(num_nodes):
        t0 = i / num_nodes
        t1 = (i + 1) / num_nodes
        ts = 1.0 - t0 * taper
        te = 1.0 - t1 * taper
        w0 = width * ts
        w1 = width * te

        nx0 = x0 + dx * t0
        ny0 = y0 + dy * t0
        nx1 = x0 + dx * t1
        ny1 = y0 + dy * t1

        draw_segment(draw, nx0, ny0, nx1, ny1, w0, w1, color)

        if i > 0:
            draw_node_ring(draw, nx0, ny0, ux, uy, px, py, w0, color)
            node_info.append((nx0, ny0, ux, uy, px, py, w0))

    return node_info


def draw_leaf_cluster_at_tip(draw, tip_x, tip_y, angle_up, num_leaves,
                             leaf_len, leaf_w, color):
    """Fan of slender leaves at a branch tip. ALL point upward.

    Leaves radiate in a ~60° fan centered on 'up' direction, adjusted
    by the branch's approach angle.
    """
    spread = math.radians(55)
    # Center direction is weighted toward straight up
    center = -math.pi / 2 + angle_up * 0.3

    for i in range(num_leaves):
        if num_leaves == 1:
            off = 0
        else:
            off = -spread / 2 + spread * i / (num_leaves - 1)

        la = center + off
        # Clamp to upward hemisphere
        if math.sin(la) > -0.03:
            la = center + off * 0.25

        bx, by = tip_x, tip_y
        tx = bx + math.cos(la) * leaf_len
        ty = by + math.sin(la) * leaf_len

        draw_slender_brush_leaf(draw, bx, by, tx, ty, leaf_w, color)


def draw_branch_from_node(draw, node_x, node_y, stalk_ux, stalk_uy,
                          stalk_px, stalk_py, stalk_w, side, color,
                          main_stalk_h, num_branch_nodes=2):
    """Draw a branch (竹枝) emerging from a main stalk node.

    Branch has its own segments + rings, then leaves at its tip.
    side = +1 (right) or -1 (left).
    """
    br_angle = math.radians(38)  # angle from stalk direction
    br_len = main_stalk_h * 0.18  # branch length proportional to stalk height

    # Branch direction: stalk direction rotated by br_angle to left/right
    stalk_angle = math.atan2(stalk_uy, stalk_ux)
    br_dir = stalk_angle + side * br_angle

    # Branch tip
    btx = node_x + math.cos(br_dir) * br_len
    bty = node_y + math.sin(br_dir) * br_len

    # Draw the branch as a mini stalk with its own nodes
    br_width = stalk_w * 0.28
    draw_stalk_with_nodes(draw, node_x, node_y, btx, bty, br_width, color,
                          num_nodes=num_branch_nodes, taper=0.10)

    # Leaves at the branch tip
    n_leaves = 3 if side > 0 else 2
    leaf_len = br_len * 1.8
    leaf_w = br_width * 1.5
    draw_leaf_cluster_at_tip(draw, btx, bty,
                             br_dir + math.pi / 2,  # upward from branch
                             n_leaves, leaf_len, leaf_w, color)


def draw_two_bamboo(draw, cx, cy, r, fg_color):
    """Two upright stalks with branches and leaves."""
    stalk_w = r * 0.048
    stalk_h = r * 1.35
    gap = r * 0.27

    for si, hs in enumerate([0.82, 1.0]):
        sx = cx + gap * (1 if si == 1 else -1)
        by = cy + r * 0.65
        ty = by - stalk_h * hs

        # Draw main stalk
        nodes = draw_stalk_with_nodes(draw, sx, by, sx, ty, stalk_w, fg_color,
                                      num_nodes=4, taper=0.15)

        # Branches from nodes: alternate sides
        # side, which_node (0=bottom, 1=middle, 2=upper), num_branch_nodes
        branch_specs = [(+1, 0, 2), (-1, 1, 2), (+1, 2, 2)]

        for side, ni, nbn in branch_specs:
            if ni < len(nodes):
                nx, ny, ux, uy, px, py, nw = nodes[ni]
                draw_branch_from_node(draw, nx, ny, ux, uy, px, py, nw,
                                      side, fg_color, stalk_h,
                                      num_branch_nodes=nbn)


# ═══════════════════════════════════════════════════════════════
# 4 color variants
# ═══════════════════════════════════════════════════════════════

def _super_scale(size):
    """Adaptive supersample factor: smaller output → higher factor."""
    if size <= 16:
        return 8
    elif size <= 24:
        return 6
    elif size <= 48:
        return 5
    else:
        return 4


def make_variant(bg_fill, bg_outline, bg_ring_w, fg_color, inner_scale=1.0):
    def draw_fn(size):
        ss = _super_scale(size)
        render_size = size * ss
        img = Image.new("RGBA", (render_size, render_size), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        m = max(2, int(render_size * 0.045))
        r = (render_size - m * 2) / 2
        cx, cy = render_size / 2, render_size / 2

        d.ellipse(_circle_bbox(cx, cy, r), fill=bg_fill)
        if bg_outline and bg_ring_w:
            actual_ring_w = max(1, int(render_size * 0.018))
            d.ellipse(_circle_bbox(cx, cy, r), outline=bg_outline,
                      width=actual_ring_w)

        draw_two_bamboo(d, cx, cy, r * inner_scale, fg_color)

        if ss > 1:
            img = img.resize((size, size), Image.LANCZOS)
        return img
    return draw_fn


VARIANTS = [
    ("V1: 墨绿底+白竹",
     make_variant((22, 65, 40, 255), None, None, (225, 235, 220, 255))),
    ("V2: 仿古纸+墨竹",
     make_variant((248, 243, 230, 255), (55, 48, 42, 255), 1,
                  (42, 38, 32, 255))),
    ("V3: 朱红印章+双竹",
     make_variant((195, 42, 42, 255), (155, 28, 28, 255), 1,
                  (252, 242, 230, 255), inner_scale=0.88)),
    ("V4: 青绿淡染+墨竹",
     make_variant((198, 218, 195, 255), None, None, (38, 58, 42, 255))),
]

USAGE = """\
gen_v5_icons.py — Generate CxxIME V5 design icons.

Usage:
  python gen_v5_icons.py                  # Generate .ico files to ./v5/
  python gen_v5_icons.py --png            # Also save 256px PNG previews
  python gen_v5_icons.py --sheet          # Also generate multi-size contact sheet
  python gen_v5_icons.py --all            # Generate everything (ico + png + sheet)
  python gen_v5_icons.py -o ../dist/icons # Custom output directory
  python gen_v5_icons.py --help           # Show this help

Output:
  v5/v5_v1.ico   Ink-green bg + white bamboo
  v5/v5_v2.ico   Antique paper + dark ink bamboo  (outlined)
  v5/v5_v3.ico   Vermillion seal + cream bamboo   (outlined)
  v5/v5_v4.ico   Celadon wash + ink bamboo

Design: Slender 6:1 lanceolate leaves, 2-node branches from stalk nodes.
"""

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate CxxIME V5 design icons",
        add_help=False)
    parser.add_argument("--help", action="store_true", help="Show usage and exit")
    parser.add_argument("--png", action="store_true", help="Save 256px PNG previews")
    parser.add_argument("--sheet", action="store_true", help="Generate contact sheet")
    parser.add_argument("--all", action="store_true", help="Generate everything")
    parser.add_argument("-o", "--output-dir", default=None,
                        help="Custom output directory for .ico files")
    args = parser.parse_args()

    if args.help:
        print(USAGE)
        exit(0)

    do_png = args.png or args.all
    do_sheet = args.sheet or args.all
    ico_dir = args.output_dir or RES_DIR
    os.makedirs(ico_dir, exist_ok=True)

    # Generate .ico files (always)
    for name, fn in VARIANTS:
        short = name.split(":")[0].strip().lower()
        images = [fn(sz) for sz in SIZES]
        ico_path = os.path.join(ico_dir, f"v5_{short}.ico")
        _write_ico(images, ico_path)
        print(f"ICO: {ico_path}")

    # PNG previews
    if do_png:
        for name, fn in VARIANTS:
            short = name.split(":")[0].strip().lower()
            img = fn(256)
            path = os.path.join(RES_DIR, f"v5_{short}.png")
            img.save(path)
            print(f"PNG: {path}")

    # Contact sheet
    if do_sheet:
        num_v = len(VARIANTS)
        cell, gap, label_h = 256, 20, 28
        tw = num_v * (cell + gap) + gap
        th = len(SIZES) * (cell + gap + label_h) + gap
        sheet = Image.new("RGBA", (tw, th), (248, 248, 248, 255))
        ds = ImageDraw.Draw(sheet)
        for vi, (name, _) in enumerate(VARIANTS):
            ds.text((gap + vi*(cell+gap) + 8, 6), name, fill=(30, 30, 30, 255))
        for ri, sz in enumerate(SIZES):
            ry = gap + label_h + ri*(cell+gap+label_h)
            ds.text((6, ry + cell//2 - 8), f"{sz}px", fill=(100, 100, 100, 255))
            for vi, (_, fn) in enumerate(VARIANTS):
                icon = fn(sz)
                ox = gap + vi*(cell+gap) + (cell-sz)//2
                oy = ry + (cell-sz)//2
                sheet.paste(icon, (ox, oy), icon)
        sp = os.path.join(RES_DIR, "v5_contact_sheet.png")
        sheet.save(sp)
        print(f"Sheet: {sp}")

    print("Done!")
