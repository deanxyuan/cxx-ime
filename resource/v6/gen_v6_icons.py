#!/usr/bin/env python3
"""
Icon preview v6 — Blade-shaped bamboo leaves (刀锋竹叶):
- Asymmetric: one edge straight/shallow, one edge curved/deep
- Needle-sharp tip, angular base with brush-press hook
- Proper branch structure: stalk → multi-node branch → leaf cluster at tip
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


def draw_blade_leaf(draw, base_x, base_y, tip_x, tip_y, max_width, color):
    """Draw a single blade-shaped bamboo leaf.

    Blade profile (刀锋形):
    - Concave edge (blade spine, 叶背): nearly straight, slight inward curve
    - Convex edge (blade belly, 叶腹): pronounced outward curve, bulging at ~1/3
    - Tip: needle-sharp, both edges converge to a point
    - Base: angular brush-press, slightly flared then cut straight
    """
    dx = tip_x - base_x
    dy = tip_y - base_y
    length = math.hypot(dx, dy)
    if length < 2:
        return

    ux, uy = dx / length, dy / length   # tip direction
    nx, ny = -uy, ux                     # normal (blade thickness direction)

    # Blade asymmetry: the "belly" bulges in the +normal direction
    # The "spine" is in the -normal direction (much flatter)

    belly_bow = length * 0.28  # how much the belly bulges outward
    spine_bow = length * 0.06  # spine is nearly straight (very subtle curve inward)

    steps = 60
    spine_pts = []  # flat/near-straight edge
    belly_pts = []  # curved/convex edge

    for i in range(steps + 1):
        t = i / steps  # 0=base, 1=tip

        cx = base_x + dx * t
        cy = base_y + dy * t

        # --- Width profile: brush press (按) then lift (提) ---
        if t < 0.03:
            # Base hook: very thin, brush just touches paper
            w = max_width * (t / 0.03) * 0.15
        elif t < 0.22:
            # Press phase: brush presses down, widens quickly
            frac = (t - 0.03) / 0.19
            w = max_width * (0.15 + 0.85 * frac)
        else:
            # Lift phase: gradual taper to needle tip
            frac = (t - 0.22) / 0.78
            w = max_width * (1.0 - frac) ** 0.45
        w = max(0.15, w)

        # --- Edge offset ---
        # Belly bows outward, spine stays nearly straight
        belly_off = belly_bow * math.sin(t * math.pi) ** 0.55
        spine_off = -spine_bow * math.sin(t * math.pi) ** 0.55

        belly_pts.append((cx + nx * (w + belly_off),
                          cy + ny * (w + belly_off)))
        spine_pts.append((cx - nx * (w + spine_off),
                          cy - ny * (w + spine_off)))

    # Build outline: go up the spine (tip→base), then down the belly (base→tip)
    # Actually: belly base→tip, then spine tip→base
    outline = belly_pts + list(reversed(spine_pts))

    if len(outline) >= 3:
        draw.polygon(outline, fill=color)

        # Midrib (叶脉): follows the spine side (closer to the straight edge),
        # slightly offset toward belly
        if length > 8:
            mid_c = tuple(min(255, c + (35 if c < 128 else -30)) for c in color[:3]) + (color[3],)
            prev = None
            for j in range(20):
                t = j / 19
                cx = base_x + dx * t
                cy = base_y + dy * t
                # Midrib sits closer to spine edge (biased -0.15 of width)
                mid_off = (belly_bow * 0.2 - spine_bow * 0.5) * math.sin(t * math.pi) ** 0.5
                pt = (cx + nx * mid_off, cy + ny * mid_off)
                if prev:
                    draw.line([prev, pt], fill=mid_c, width=1)
                prev = pt


def draw_segment(draw, x0, y0, x1, y1, w0, w1, color):
    """Internode segment."""
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    if length < 0.5:
        return
    ux, uy = dx / length, dy / length
    px, py = -uy, ux
    if w0 > 0.3 and w1 > 0.3:
        draw.polygon([
            (x0 + px * w0 / 2, y0 + py * w0 / 2),
            (x1 + px * w1 / 2, y1 + py * w1 / 2),
            (x1 - px * w1 / 2, y1 - py * w1 / 2),
            (x0 - px * w0 / 2, y0 - py * w0 / 2),
        ], fill=color)
        hl = tuple(min(255, c + 30) for c in color[:3]) + (color[3],)
        hl_w = max(1, int(w0 * 0.2))
        draw.line([(x0 + px * w0 / 5, y0 + py * w0 / 5),
                    (x1 + px * w1 / 5, y1 + py * w1 / 5)], fill=hl, width=hl_w)


def draw_node_ring(draw, x, y, ux, uy, px, py, stalk_w, color):
    """Node ring."""
    nrw = stalk_w * 1.8
    nrh = max(1.0, stalk_w * 0.55)
    draw.polygon([
        (x + px * nrw / 2, y + py * nrw / 2),
        (x + px * nrw / 2 + ux * nrh, y + py * nrw / 2 + uy * nrh),
        (x - px * nrw / 2 + ux * nrh, y - py * nrw / 2 + uy * nrh),
        (x - px * nrw / 2, y - py * nrw / 2),
    ], fill=color)
    bright = tuple(min(255, c + 60) for c in color[:3]) + (color[3],)
    draw.line([(x + px * nrw / 2, y + py * nrw / 2),
                (x - px * nrw / 2, y - py * nrw / 2)],
              fill=bright, width=max(1, int(nrh * 0.3)))


def draw_stalk(draw, x0, y0, x1, y1, width, color, num_nodes, taper=0.15):
    """Draw stalk with segments + rings. Returns list of node info."""
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    px, py = -uy, ux
    nodes = []
    for i in range(num_nodes):
        t0, t1 = i / num_nodes, (i + 1) / num_nodes
        ts, te = 1.0 - t0 * taper, 1.0 - t1 * taper
        w0, w1 = width * ts, width * te
        nx0, ny0 = x0 + dx * t0, y0 + dy * t0
        nx1, ny1 = x0 + dx * t1, y0 + dy * t1
        draw_segment(draw, nx0, ny0, nx1, ny1, w0, w1, color)
        if i > 0:
            draw_node_ring(draw, nx0, ny0, ux, uy, px, py, w0, color)
            nodes.append((nx0, ny0, ux, uy, px, py, w0))
    return nodes


def draw_leaf_cluster(draw, tip_x, tip_y, approach_angle, num_leaves,
                      leaf_len, leaf_w, color):
    """Blade leaves in upward fan from branch tip."""
    spread = math.radians(50)
    center = -math.pi / 2 + approach_angle * 0.3
    for i in range(num_leaves):
        if num_leaves == 1:
            off = 0
        else:
            off = -spread / 2 + spread * i / (num_leaves - 1)
        la = center + off
        if math.sin(la) > -0.02:
            la = center + off * 0.2  # force upward
        tx = tip_x + math.cos(la) * leaf_len
        ty = tip_y + math.sin(la) * leaf_len
        draw_blade_leaf(draw, tip_x, tip_y, tx, ty, leaf_w, color)


def draw_branch(draw, node_x, node_y, stalk_ux, stalk_uy,
                stalk_px, stalk_py, stalk_w, side, color,
                stalk_h, num_br_nodes=2):
    """Branch from main stalk node: has its own segments+rings, leaves at tip."""
    stalk_angle = math.atan2(stalk_uy, stalk_ux)
    br_angle = stalk_angle + side * math.radians(40)
    br_len = stalk_h * 0.18
    btx = node_x + math.cos(br_angle) * br_len
    bty = node_y + math.sin(br_angle) * br_len

    br_w = stalk_w * 0.28
    draw_stalk(draw, node_x, node_y, btx, bty, br_w, color,
               num_nodes=num_br_nodes, taper=0.10)

    n_leaves = 3 if side > 0 else 2
    leaf_len = br_len * 1.9
    leaf_w = br_w * 1.6
    draw_leaf_cluster(draw, btx, bty, br_angle + math.pi / 2,
                      n_leaves, leaf_len, leaf_w, color)


def draw_two_bamboo(draw, cx, cy, r, fg_color):
    """Two stalks, upright, left shorter."""
    stalk_w = r * 0.048
    stalk_h = r * 1.35
    gap = r * 0.27

    for si, hs in enumerate([0.82, 1.0]):
        sx = cx + gap * (1 if si == 1 else -1)
        by = cy + r * 0.65
        ty = by - stalk_h * hs
        nodes = draw_stalk(draw, sx, by, sx, ty, stalk_w, fg_color, 4, 0.15)
        for side, ni, nbn in [(+1, 0, 2), (-1, 1, 2), (+1, 2, 2)]:
            if ni < len(nodes):
                nx, ny, ux, uy, px, py, nw = nodes[ni]
                draw_branch(draw, nx, ny, ux, uy, px, py, nw,
                            side, fg_color, stalk_h, nbn)


# ═══════════════════════════════════════════
# Variants
# ═══════════════════════════════════════════

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


def _mk(bg, ol, fg, iscale=1.0, ol_w=None):
    def fn(size):
        ss = _super_scale(size)
        render_size = size * ss
        img = Image.new("RGBA", (render_size, render_size), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        m = max(2, int(render_size * 0.045))
        r = (render_size - m * 2) / 2
        cx, cy = render_size / 2, render_size / 2
        d.ellipse(_circle_bbox(cx, cy, r), fill=bg)
        if ol and ol_w:
            d.ellipse(_circle_bbox(cx, cy, r), outline=ol, width=max(1, int(render_size * 0.018)))
        draw_two_bamboo(d, cx, cy, r * iscale, fg)
        if ss > 1:
            img = img.resize((size, size), Image.LANCZOS)
        return img
    return fn

VARIANTS = [
    ("V1: 墨绿底+白竹", _mk((22, 65, 40, 255), None, (225, 235, 220, 255))),
    ("V2: 仿古纸+墨竹", _mk((248, 243, 230, 255), (55, 48, 42, 255), (42, 38, 32, 255))),
    ("V3: 朱红印章+双竹", _mk((195, 42, 42, 255), (155, 28, 28, 255), (252, 242, 230, 255), 0.88)),
    ("V4: 青绿淡染+墨竹", _mk((198, 218, 195, 255), None, (38, 58, 42, 255))),
]

USAGE = """\
gen_v6_icons.py — Generate CxxIME V6 design icons.

Usage:
  python gen_v6_icons.py                  # Generate .ico files to ./v6/
  python gen_v6_icons.py --png            # Also save 256px PNG previews
  python gen_v6_icons.py --sheet          # Also generate multi-size contact sheet
  python gen_v6_icons.py --all            # Generate everything (ico + png + sheet)
  python gen_v6_icons.py -o ../dist/icons # Custom output directory
  python gen_v6_icons.py --help           # Show this help

Output:
  v6/v6_v1.ico   Ink-green bg + white bamboo
  v6/v6_v2.ico   Antique paper + dark ink bamboo  (outlined)
  v6/v6_v3.ico   Vermillion seal + cream bamboo   (outlined)
  v6/v6_v4.ico   Celadon wash + ink bamboo

Design: Asymmetric blade-shaped leaves (belly curved, spine straight).
"""

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate CxxIME V6 design icons",
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
        ico_path = os.path.join(ico_dir, f"v6_{short}.ico")
        _write_ico(images, ico_path)
        print(f"ICO: {ico_path}")

    # PNG previews
    if do_png:
        for name, fn in VARIANTS:
            short = name.split(":")[0].strip().lower()
            img = fn(256)
            path = os.path.join(RES_DIR, f"v6_{short}.png")
            img.save(path)
            print(f"PNG: {path}")

    # Contact sheet
    if do_sheet:
        num_v, cell, gap, lh = len(VARIANTS), 256, 20, 28
        tw = num_v * (cell + gap) + gap
        th = len(SIZES) * (cell + gap + lh) + gap
        sheet = Image.new("RGBA", (tw, th), (248, 248, 248, 255))
        ds = ImageDraw.Draw(sheet)
        for vi, (name, _) in enumerate(VARIANTS):
            ds.text((gap + vi*(cell+gap) + 8, 6), name, fill=(30, 30, 30, 255))
        for ri, sz in enumerate(SIZES):
            ry = gap + lh + ri*(cell+gap+lh)
            ds.text((6, ry + cell//2 - 8), f"{sz}px", fill=(100, 100, 100, 255))
            for vi, (_, fn) in enumerate(VARIANTS):
                icon = fn(sz)
                ox = gap + vi*(cell+gap) + (cell-sz)//2
                oy = ry + (cell-sz)//2
                sheet.paste(icon, (ox, oy), icon)
        sp = os.path.join(RES_DIR, "v6_contact_sheet.png")
        sheet.save(sp)
        print(f"Sheet: {sp}")

    print("Done!")
