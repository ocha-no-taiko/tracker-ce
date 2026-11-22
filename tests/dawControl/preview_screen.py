#!/usr/bin/env python3
"""Draws the display list written by screen_preview.cpp into an 800x480 PNG,
with the glyphs of the .ptfont the screen uses. Optional: a mockup image to
place beside it for comparison.

    python3 preview_screen.py list.txt LiveTracker.ptfont out.png [mockup.png]
"""
import struct
import sys

from PIL import Image, ImageDraw


def load_glyphs(path):
    data = open(path, "rb").read()
    count = struct.unpack_from("<H", data, 6)[0]
    fonts = {}
    for i in range(count):
        offset, size, handle, _ = struct.unpack_from("<IIHH", data, 16 + i * 12)
        block = data[offset:offset + size]
        widths = list(block[:128])
        _, stride, width, height, _ = struct.unpack_from("<5I", block, 128)
        first = min(c for c in range(128) if widths[c])
        fonts[handle] = (stride, width, height, first, block[148:])
    return fonts


def glyph_mask(font, cell):
    stride, width, height, first, pixels = font
    mask = Image.new("L", (width, height), 0)
    if cell < first:
        return mask
    base = (cell - first) * stride * height
    for y in range(height):
        for x in range(width):
            b = pixels[base + y * stride + x // 2]
            level = (b >> 4) if x % 2 == 0 else (b & 15)
            mask.putpixel((x, y), level * 17)
    return mask


def render(list_path, font_path):
    fonts = load_glyphs(font_path)
    cache = {}
    image = Image.new("RGB", (800, 480), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    color = (255, 255, 255)
    prim = None
    verts = []
    handle = 1
    cell = 0
    for line in open(list_path):
        op, *args = line.split()
        args = [int(a) for a in args]
        if op == "color":
            v = args[0]
            color = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
        elif op == "begin":
            prim, verts = args[0], []
        elif op == "handle":
            handle = args[0]
        elif op == "cell":
            cell = args[0]
        elif op in ("v2f", "v2ii"):
            x, y = args[0], args[1]
            if prim == 1:  # bitmaps
                h, c = (args[2], args[3]) if op == "v2ii" else (handle, cell)
                key = (h, c)
                if key not in cache:
                    cache[key] = glyph_mask(fonts[h], c)
                image.paste(Image.new("RGB", cache[key].size, color), (x, y), cache[key])
                continue
            verts.append((x, y))
            if prim == 9 and len(verts) == 2:
                draw.rectangle(verts, fill=color)
                verts = []
            elif prim == 3 and len(verts) == 2:
                draw.line(verts, fill=color, width=1)
                verts = []
        elif op == "end":
            if prim == 4 and len(verts) > 1:
                draw.line(verts, fill=color, width=1)
            prim = None
    return image


def main():
    image = render(sys.argv[1], sys.argv[2])
    if len(sys.argv) > 4:
        mock = Image.open(sys.argv[4]).convert("RGB").resize((800, 480), Image.LANCZOS)
        both = Image.new("RGB", (800, 970), (60, 60, 60))
        both.paste(image, (0, 0))
        both.paste(mock, (0, 490))
        both.save(sys.argv[3])
    else:
        image.save(sys.argv[3])


if __name__ == "__main__":
    main()
