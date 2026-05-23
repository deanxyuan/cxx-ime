#!/usr/bin/env python3
"""Generate CxxIME icons using Pillow"""
import os
from PIL import Image, ImageDraw, ImageFont

def create_icon(text, bg_color, text_color, filename):
    """Create a simple icon with text in multiple sizes."""
    # Create icons in multiple sizes for better compatibility
    sizes = [16, 32, 48]
    images = []

    for size in sizes:
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)

        # Draw filled rectangle background
        draw.rectangle([0, 0, size-1, size-1], fill=bg_color + (255,))

        # Scale font size proportionally
        font_size = max(8, size // 2)
        try:
            font = ImageFont.truetype("arial.ttf", font_size)
        except:
            font = ImageFont.load_default()

        # Center text
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        x = (size - tw) // 2
        y = (size - th) // 2 - bbox[1]

        draw.text((x, y), text, fill=text_color + (255,), font=font)
        images.append(img)

    # Save as ICO with multiple sizes
    images[0].save(filename, format='ICO', sizes=[(s, s) for s in sizes], append_images=images[1:])
    print(f"Created: {filename} ({', '.join(str(s)+'x'+str(s) for s in sizes)})")

res_dir = os.path.dirname(os.path.abspath(__file__))

# zh.ico - bright green background, white text
create_icon("zh", (0, 160, 0), (255, 255, 255), os.path.join(res_dir, "zh.ico"))

# en.ico - bright blue background, white text
create_icon("EN", (0, 100, 200), (255, 255, 255), os.path.join(res_dir, "en.ico"))

# cxxime.ico - dark blue background, white text
create_icon("Cxx", (0, 80, 160), (255, 255, 255), os.path.join(res_dir, "cxxime.ico"))

print("All icons generated!")
