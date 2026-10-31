#!/usr/bin/env python3
# Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

"""Generate NSIS artwork on Windows using Python's stdlib and system fonts.

Run: python scripts/generate_installer_artwork.py
The committed BMPs are build inputs; regeneration is not part of packaging.
"""

import ctypes as ct
from ctypes import wintypes as wt
from pathlib import Path
import struct
import sys


OUTPUT = Path(__file__).resolve().parents[1] / "docs" / "installer"
SCALE = 4
INK = "353431"
MUTED = "4F604D"
ACCENT = "B66D4E"
PAPER = "F7F4ED"
SAGE = "B0C2AC"


def color(hex_color):
    red, green, blue = bytes.fromhex(hex_color)
    return red | green << 8 | blue << 16


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


class Canvas:
    """Small GDI drawing surface; coordinates use NSIS's nominal bitmap sizes."""

    def __init__(self, width, height):
        self.width = width * SCALE
        self.height = height * SCALE
        gdi = ct.WinDLL("gdi32", use_last_error=True)
        self.delete = bind(gdi, "DeleteObject", wt.BOOL, wt.HANDLE)
        self.select = bind(gdi, "SelectObject", wt.HANDLE, wt.HDC, wt.HANDLE)
        self.delete_dc = bind(gdi, "DeleteDC", wt.BOOL, wt.HDC)
        self.brush = bind(gdi, "CreateSolidBrush", wt.HANDLE, wt.DWORD)
        self.round_rect = bind(
            gdi,
            "RoundRect",
            wt.BOOL,
            wt.HDC,
            ct.c_int,
            ct.c_int,
            ct.c_int,
            ct.c_int,
            ct.c_int,
            ct.c_int,
        )
        self.text_out = bind(
            gdi,
            "TextOutW",
            wt.BOOL,
            wt.HDC,
            ct.c_int,
            ct.c_int,
            wt.LPCWSTR,
            ct.c_int,
        )
        self.text_color = bind(gdi, "SetTextColor", wt.DWORD, wt.HDC, wt.DWORD)
        self.create_font = bind(
            gdi,
            "CreateFontW",
            wt.HANDLE,
            *([ct.c_int] * 5),
            *([wt.DWORD] * 8),
            wt.LPCWSTR,
        )
        self.dc = bind(gdi, "CreateCompatibleDC", wt.HDC, wt.HDC)(None)
        # A top-down 32-bit DIB exposes its pixels without screen capture.
        info = ct.create_string_buffer(
            struct.pack(
                "<IiiHHIIiiII",
                40,
                self.width,
                -self.height,
                1,
                32,
                0,
                0,
                0,
                0,
                0,
                0,
            )
        )
        pixels = ct.c_void_p()
        self.bitmap = bind(
            gdi,
            "CreateDIBSection",
            wt.HANDLE,
            wt.HDC,
            ct.c_void_p,
            wt.UINT,
            ct.POINTER(ct.c_void_p),
            wt.HANDLE,
            wt.DWORD,
        )(self.dc, info, 0, ct.byref(pixels), None, 0)
        if not self.dc or not self.bitmap:
            raise ct.WinError(ct.get_last_error())
        self.pixels = pixels
        self.old_bitmap = self.select(self.dc, self.bitmap)
        null_pen = bind(gdi, "GetStockObject", wt.HANDLE, ct.c_int)(8)
        self.old_pen = self.select(self.dc, null_pen)
        bind(gdi, "SetBkMode", ct.c_int, wt.HDC, ct.c_int)(self.dc, 1)
        self.flush = bind(gdi, "GdiFlush", wt.BOOL)

    def box(self, bounds, fill, radius=0):
        brush = self.brush(color(fill))
        previous = self.select(self.dc, brush)
        coordinates = [round(value * SCALE) for value in bounds]
        self.round_rect(
            self.dc,
            *coordinates,
            radius * SCALE * 2,
            radius * SCALE * 2,
        )
        self.select(self.dc, previous)
        self.delete(brush)

    def text(self, x, y, text, size, fill=INK, bold=False):
        font = self.create_font(
            -round(size * SCALE),
            0,
            0,
            0,
            700 if bold else 400,
            0,
            0,
            0,
            1,
            0,
            0,
            4,
            0,
            "Microsoft YaHei",
        )
        previous = self.select(self.dc, font)
        self.text_color(self.dc, color(fill))
        self.text_out(
            self.dc,
            round(x * SCALE),
            round(y * SCALE),
            text,
            len(text),
        )
        self.select(self.dc, previous)
        self.delete(font)

    def save(self, name):
        self.flush()
        raw = ct.string_at(self.pixels, self.width * self.height * 4)
        # Box-filter 4x drawing to a 2x asset. GDI uses grayscale text antialiasing.
        width, height = self.width // 2, self.height // 2
        rgb = bytearray(width * height * 3)
        for y in range(height):
            for x in range(width):
                source = (y * 2 * self.width + x * 2) * 4
                target = (y * width + x) * 3
                for channel in range(3):
                    offset = source + 2 - channel
                    rgb[target + channel] = sum(
                        raw[offset + delta]
                        for delta in (0, 4, self.width * 4, self.width * 4 + 4)
                    ) // 4
        stride = (width * 3 + 3) & ~3
        bitmap = bytearray()
        for y in reversed(range(height)):
            row = rgb[y * width * 3:(y + 1) * width * 3]
            for x in range(0, len(row), 3):
                bitmap.extend(row[x:x + 3][::-1])
            bitmap.extend(bytes(stride - width * 3))
        header = struct.pack("<2sIHHI", b"BM", 54 + len(bitmap), 0, 0, 54)
        header += struct.pack(
            "<IiiHHIIiiII",
            40,
            width,
            height,
            1,
            24,
            0,
            len(bitmap),
            0,
            0,
            0,
            0,
        )
        (OUTPUT / f"{name}.bmp").write_bytes(header + bitmap)

        print(f"{name}: {width} x {height}, RGB BMP")

    def close(self):
        self.select(self.dc, self.old_pen)
        self.select(self.dc, self.old_bitmap)
        self.delete(self.bitmap)
        self.delete_dc(self.dc)


def draw_wizard(canvas):
    canvas.box((0, 0, 165, 315), SAGE)
    # Broad, low-contrast curves frame the illustration without adding more UI symbols.
    canvas.box((61, -76, 281, 144), "BECEB8", 110)
    canvas.box((-144, 163, 88, 395), "A3B79F", 116)

    # A short preedit and three candidates form one recognizable input-method scene.
    canvas.box((17, 65, 128, 106), "97AC91", 5)
    canvas.box((18, 66, 127, 105), PAPER, 4)
    canvas.text(28, 73, "wen", 20, INK)
    canvas.box((70, 78, 72, 98), ACCENT)

    # A restrained layered shadow lifts only the candidate panel above the input surface.
    canvas.box((26, 112, 153, 225), "A8BBA4", 9)
    canvas.box((28, 112, 151, 223), "A2B59E", 8)
    canvas.box((30, 112, 149, 221), "9CAE97", 7)
    canvas.box((28, 109, 148, 218), PAPER, 6)
    canvas.box((34, 116, 142, 146), MUTED, 3)
    canvas.text(44, 122, "1", 12, PAPER)
    canvas.text(67, 117, "文", 21, PAPER)
    canvas.text(44, 156, "2", 12, MUTED)
    canvas.text(67, 151, "问", 21, INK)
    canvas.text(44, 189, "3", 12, MUTED)
    canvas.text(67, 184, "闻", 21, INK)

    canvas.text(19, 259, "CxxIME", 22, INK, True)


def draw_header(canvas):
    canvas.box((0, 0, 151, 58), "FFFFFF")
    canvas.box((5, 9, 41, 45), SAGE, 2)
    canvas.text(10, 12, "文", 24, INK)
    canvas.text(51, 16, "CxxIME", 19, INK, True)


def main():
    if sys.platform != "win32":
        raise SystemExit("Regeneration requires Windows and the Microsoft YaHei system font.")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name, width, height, draw in (
        ("wizard", 164, 314, draw_wizard),
        ("header", 150, 57, draw_header),
    ):
        canvas = Canvas(width, height)
        try:
            draw(canvas)
            canvas.save(name)
        finally:
            canvas.close()


if __name__ == "__main__":
    main()
