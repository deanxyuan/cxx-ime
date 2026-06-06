#!/usr/bin/env python3
"""
Icon preview v4 — Correct Chinese ink-painting bamboo:
- All leaves point upward/outward (never droop)
- Small angled branches (竹枝) emerge from nodes, leaves sprout from branch tips
- Classic 个字/介字/分字 upward-fan patterns
- Two upright stalks, left shorter
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


def draw_brush_leaf(draw, base_x, base_y, tip_x, tip_y, max_width, color):
    """A single brush-stroke bamboo leaf: curved, tapered, sharp tip, slight base hook.

    Base is always BELOW the tip (leaf points upward).
    """
    dx = tip_x - base_x
    dy = tip_y - base_y
    length = math.hypot(dx, dy)
    if length < 2:
        return

    ux, uy = dx / length, dy / length   # chord direction (base → tip)
    px, py = -uy, ux                     # perpendicular

    # Arch bow — leaf curves outward from chord, peaking near base
    bow = length * 0.15

    steps = 50
    left_pts = []
    right_pts = []

    for i in range(steps + 1):
        t = i / steps  # 0=base, 1=tip

        cx = base_x + dx * t
        cy = base_y + dy * t

        # Bow peaks at t≈0.25, zero at ends
        bow_off = bow * math.sin(t * math.pi) ** 0.65

        # Width profile: quick press, long taper
        if t < 0.06:
            w = max_width * (t / 0.06) * 0.25
        elif t < 0.28:
            frac = (t - 0.06) / 0.22
            w = max_width * (0.25 + 0.75 * frac)
        else:
            frac = (t - 0.28) / 0.72
            w = max_width * (1.0 - frac) ** 0.55
        w = max(0.3, w)

        left_pts.append((cx + px * (w + bow_off), cy + py * (w + bow_off)))
        right_pts.insert(0, (cx - px * (w - bow_off * 0.5), cy - py * (w - bow_off * 0.5)))

    outline = left_pts + right_pts
    if len(outline) >= 3:
        draw.polygon(outline, fill=color)

        # Midrib
        if length > 10:
            mid_c = tuple(min(255, c + (35 if c < 128 else -25)) for c in color[:3]) + (color[3],)
            mid_pts = []
            for i in range(22):
                t = i / 21
                cx = base_x + dx * t
                cy = base_y + dy * t
                bo = bow * math.sin(t * math.pi) ** 0.65 * 0.45
                mid_pts.append((cx + px * bo, cy + py * bo))
            for i in range(len(mid_pts) - 1):
                draw.line([mid_pts[i], mid_pts[i + 1]], fill=mid_c, width=1)


def draw_branch(draw, node_x, node_y, stalk_angle, side, branch_len, branch_angle, color):
    """Draw a short angled branch (竹枝) emerging from a node.

    Branches alternate sides and angle upward/outward.
    Returns (tip_x, tip_y) where leaves should attach.
    """
    # Branch direction: upward and to the side
    # stalk_angle = -π/2 for stalks growing upward
    # side = -1 (left) or +1 (right)
    base_angle = stalk_angle  # stalk direction
    branch_a = base_angle + side * branch_angle  # upward + outward

    tip_x = node_x + math.cos(branch_a) * branch_len
    tip_y = node_y + math.sin(branch_a) * branch_len

    # Draw branch as a thin tapered line
    bw = max(0.6, branch_len * 0.06)
    # Simple line with slight thickness
    draw.line([(node_x, node_y), (tip_x, tip_y)], fill=color,
              width=max(1, int(bw)))

    return tip_x, tip_y


def draw_leaf_cluster(draw, branch_tip_x, branch_tip_y, branch_angle,
                      num_leaves, leaf_len, leaf_width, color):
    """Draw a fan of brush-stroke leaves from a branch tip.

    ALL leaves point upward. The fan spreads ±40° from the branch direction,
    centered on upward. No leaf ever points below horizontal.
    """
    # Fan spread: leaves radiate from branch tip, all pointing upward
    spread = math.radians(70)  # total angular spread
    # The center direction is "up" (-π/2), adjusted slightly by branch angle
    center_angle = -math.pi / 2 + (branch_angle + math.pi / 2) * 0.35

    for i in range(num_leaves):
        if num_leaves == 1:
            offset = 0
        else:
            offset = -spread / 2 + spread * i / (num_leaves - 1)

        leaf_angle = center_angle + offset

        # NEVER let a leaf point downward — clamp to upward hemisphere
        # cos(angle) determines horizontal; negative cos = pointing right/up is fine
        # sin(angle) must be < 0 to point upward
        if math.sin(leaf_angle) > -0.05:
            leaf_angle = -math.pi / 2 + offset * 0.3  # force upward

        # Base of leaf at branch tip
        bx = branch_tip_x
        by = branch_tip_y

        # Tip extends upward and outward
        tx = bx + math.cos(leaf_angle) * leaf_len
        ty = by + math.sin(leaf_angle) * leaf_len

        draw_brush_leaf(draw, bx, by, tx, ty, leaf_width, color)


def draw_bamboo_stalk(draw, base_x, base_y, tip_x, tip_y, stalk_width, color,
                      num_nodes=4):
    """Draw a segmented bamboo stalk with branches and leaves at nodes.

    Branches alternate left/right. Each branch gets a leaf cluster at its tip.
    """
    dx = tip_x - base_x
    dy = tip_y - base_y
    length = math.hypot(dx, dy)
    if length < 2:
        return
    ux, uy = dx / length, dy / length
    px, py = -uy, ux
    stalk_angle = math.atan2(dy, dx)

    node_ring_w_factor = 1.7

    # Branch configuration for each node (bottom 3 nodes)
    # (side, branch_length_ratio, branch_angle, num_leaves)
    branch_configs = [
        (+1, 0.55, math.radians(42), 3),  # bottom node: right branch, 3 leaves
        (-1, 0.45, math.radians(38), 3),  # middle node: left branch, 3 leaves
        (+1, 0.35, math.radians(35), 2),  # upper node: right branch, 2 leaves
    ]

    ring_thick = max(1.0, stalk_width * 0.5)

    for i in range(num_nodes):
        t0 = i / num_nodes
        t1 = (i + 1) / num_nodes

        taper_s = 1.0 - t0 * 0.15
        taper_e = 1.0 - t1 * 0.15
        sw0 = stalk_width * taper_s
        sw1 = stalk_width * taper_e

        x0 = base_x + dx * t0
        y0 = base_y + dy * t0
        x1 = base_x + dx * t1
        y1 = base_y + dy * t1

        # Internode
        if sw0 > 0.5 and sw1 > 0.5:
            corners = [
                (x0 + px * sw0 / 2, y0 + py * sw0 / 2),
                (x1 + px * sw1 / 2, y1 + py * sw1 / 2),
                (x1 - px * sw1 / 2, y1 - py * sw1 / 2),
                (x0 - px * sw0 / 2, y0 - py * sw0 / 2),
            ]
            draw.polygon(corners, fill=color)
            hl_c = tuple(min(255, c + 35) for c in color[:3]) + (color[3],)
            hl_w = max(1, int(sw0 * 0.25))
            draw.line([(x0 + px * sw0 / 4, y0 + py * sw0 / 4),
                        (x1 + px * sw1 / 4, y1 + py * sw1 / 4)],
                      fill=hl_c, width=hl_w)

        # Node ring
        if i > 0:
            nrw = stalk_width * node_ring_w_factor * taper_s
            nrh = ring_thick
            rc = [
                (x0 + px * nrw / 2, y0 + py * nrw / 2),
                (x0 + px * nrw / 2 + ux * nrh, y0 + py * nrw / 2 + uy * nrh),
                (x0 - px * nrw / 2 + ux * nrh, y0 - py * nrw / 2 + uy * nrh),
                (x0 - px * nrw / 2, y0 - py * nrw / 2),
            ]
            draw.polygon(rc, fill=color)
            bright = tuple(min(255, c + 55) for c in color[:3]) + (color[3],)
            draw.line([(x0 + px * nrw / 2, y0 + py * nrw / 2),
                        (x0 - px * nrw / 2, y0 - py * nrw / 2)],
                      fill=bright, width=max(1, int(nrh * 0.35)))

        # Branch + leaf cluster at this node
        if i > 0 and i - 1 < len(branch_configs):
            side, br_ratio, br_angle, n_leaf = branch_configs[i - 1]
            br_len = length / num_nodes * br_ratio
            leaf_len = br_len * 1.7
            leaf_w = stalk_width * 1.4

            nx = x0 + ux * ring_thick * 0.4
            ny = y0 + uy * ring_thick * 0.4

            btx, bty = draw_branch(draw, nx, ny, stalk_angle, side,
                                   br_len, br_angle, color)
            draw_leaf_cluster(draw, btx, bty, stalk_angle + side * br_angle,
                              n_leaf, leaf_len, leaf_w, color)


def draw_two_stalks(draw, cx, cy, r, fg_color):
    """Two upright stalks: left shorter, right taller."""
    stalk_w = r * 0.045
    stalk_h = r * 1.35
    gap = r * 0.27

    for si, hs in enumerate([0.82, 1.0]):
        sx = cx + gap * (1 if si == 1 else -1)
        by = cy + r * 0.65
        ty = by - stalk_h * hs
        draw_bamboo_stalk(draw, sx, by, sx, ty, stalk_w, fg_color, num_nodes=4)


# ═══════════════════════════════════════════════════════════════
# 4 color variants (same as v3, same compositions)
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
            d.ellipse(_circle_bbox(cx, cy, r), outline=bg_outline, width=bg_ring_w)

        effective_r = r * inner_scale
        draw_two_stalks(d, cx, cy, effective_r, fg_color)

        if ss > 1:
            img = img.resize((size, size), Image.LANCZOS)
        return img
    return draw_fn


VARIANTS = [
    ("V1: 墨绿底+白竹",
     make_variant(bg_fill=(22, 65, 40, 255), bg_outline=None, bg_ring_w=None,
                  fg_color=(225, 235, 220, 255))),

    ("V2: 仿古纸+墨竹",
     make_variant(bg_fill=(248, 243, 230, 255),
                  bg_outline=(55, 48, 42, 255), bg_ring_w=max(1, int(256 * 0.018)),
                  fg_color=(42, 38, 32, 255))),

    ("V3: 朱红印章+双竹",
     make_variant(bg_fill=(195, 42, 42, 255),
                  bg_outline=(155, 28, 28, 255), bg_ring_w=max(2, int(256 * 0.05)),
                  fg_color=(252, 242, 230, 255), inner_scale=0.88)),

    ("V4: 青绿淡染+墨竹",
     make_variant(bg_fill=(198, 218, 195, 255), bg_outline=None, bg_ring_w=None,
                  fg_color=(38, 58, 42, 255))),
]


USAGE = """\
gen_v4_icons.py — Generate CxxIME V4 design icons.

Usage:
  python gen_v4_icons.py                  # Generate .ico files to ./v4/
  python gen_v4_icons.py --png            # Also save 256px PNG previews
  python gen_v4_icons.py --sheet          # Also generate multi-size contact sheet
  python gen_v4_icons.py --all            # Generate everything (ico + png + sheet)
  python gen_v4_icons.py -o ../dist/icons # Custom output directory
  python gen_v4_icons.py --help           # Show this help

Output:
  v4/v4_v1.ico   Ink-green bg + white bamboo
  v4/v4_v2.ico   Antique paper + dark ink bamboo  (outlined)
  v4/v4_v3.ico   Vermillion seal + cream bamboo   (outlined)
  v4/v4_v4.ico   Celadon wash + ink bamboo

Design: Curved brush-stroke leaves, branches from stalk nodes, leaves all upward.
"""

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate CxxIME V4 design icons",
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
        ico_path = os.path.join(ico_dir, f"v4_{short}.ico")
        _write_ico(images, ico_path)
        print(f"ICO: {ico_path}")

    # PNG previews
    if do_png:
        for name, fn in VARIANTS:
            short = name.split(":")[0].strip().lower()
            img = fn(256)
            path = os.path.join(RES_DIR, f"v4_{short}.png")
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
        sp = os.path.join(RES_DIR, "v4_contact_sheet.png")
        sheet.save(sp)
        print(f"Sheet: {sp}")

    print("Done!")
