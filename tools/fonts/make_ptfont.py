#!/usr/bin/env python3
"""Converts a TTF/OTF font family into a Polyend Tracker .ptfont package.

The Tracker's display chip (FT812) uses five legacy L4 fonts (4 bits per
pixel). This tool renders the printable ASCII characters into cells with the
same size and baseline as the built-in Roboto Mono fonts, so the result can
be used for the whole Tracker UI (Config > General > Display font) as well as for the
DAW Control screen. Characters below 32 that the built-in fonts use as icons
are copied from Roboto Mono, and character 127 holds a copyright sign (the
DAW Control screen draws UTF-8 "©" with it).

    uv run --with pillow tools/fonts/make_ptfont.py \\
        tools/fonts/AnonymousPro/AnonymousPro-Regular.ttf \\
        tools/fonts/AnonymousPro/AnonymousPro-Bold.ttf \\
        sd-card/Fonts/AnonymousPro.ptfont [--preview preview.png]

    # the font of the DAW Control screen
    uv run --with pillow tools/fonts/make_ptfont.py --layout daw \\
        tools/fonts/AnonymousPro/AnonymousPro-Regular.ttf \\
        tools/fonts/AnonymousPro/AnonymousPro-Bold.ttf \\
        sd-card/LiveTracker.ptfont

File layout (see cDisplay::loadCustomFontFromSD in core/libs/NewDisplay/display.cpp):
    header  "PTFN", u16 version 1, u16 font count 5, 8 reserved bytes
    5 x     u32 offset, u32 size, u16 handle, u16 format
    5 x     148-byte metric block (128 widths, format, line stride, width,
            height, pointer as u32) + glyphs from the first character to 127
"""

import argparse
import os
import re
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
BUILTIN = os.path.join(HERE, "..", "..", "core", "libs", "fonts")

L4 = 2
COPYRIGHT_CODE = 127

# One entry per display font slot, in the order of controlStyleFont1..5.
# cell / baseline / cap height are those of the built-in Roboto Mono set.
SLOTS = [
    {"name": "14 Regular", "bold": False, "builtin": "RobotoMono-Light_14_L4.cpp"},
    {"name": "17 Regular", "bold": False, "builtin": "RobotoMono-Light_17_L4.cpp"},
    {"name": "14 Bold", "bold": True, "builtin": "RobotoMono-Regular_14_L4.cpp"},
    {"name": "17 Bold", "bold": True, "builtin": "RobotoMono-Bold_17_L4.cpp"},
    {"name": "12 Regular", "bold": False, "builtin": "RobotoMono-Regular_12_L4.cpp"},
]

# --layout daw: the font the DAW Control screen loads while it is open
# (LiveTracker.ptfont). Each slot is sized by its character advance, and the
# cell is made so that the capitals sit in its vertical middle; the screen
# then centres a line of text on a y coordinate by drawing it half a cell
# higher. The order is the slot order the screen uses (see dawScreen.cpp).
DAW_SLOTS = [
    {"name": "main", "bold": False, "advance": 9},
    {"name": "small", "bold": False, "advance": 6},
    {"name": "tiny", "bold": False, "advance": 5},
    {"name": "main bold", "bold": True, "advance": 9},
    {"name": "small bold", "bold": True, "advance": 6},
]


def load_builtin(file_name):
    """Decodes a built-in RLE font (core/libs/fonts/*.cpp) into its raw block."""
    with open(os.path.join(BUILTIN, file_name), encoding="utf-8") as f:
        text = f.read()
    data = bytearray()
    for value, count in re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", text):
        data += bytes([int(value)]) * int(count)
    widths = list(data[:128])
    fmt, stride, width, height = struct.unpack("<4I", data[128:144])
    first = min(i for i in range(128) if widths[i])
    glyphs = {}
    for code in range(first, 128):
        if not widths[code]:
            continue
        offset = 148 + (code - first) * stride * height
        glyphs[code] = bytes(data[offset:offset + stride * height])

    def rows(ch):
        g = glyphs[ord(ch)]
        return [y for y in range(height) if any(g[y * stride:(y + 1) * stride])]

    a_rows = rows("A")
    return {
        "widths": widths, "stride": stride, "width": width, "height": height,
        "first": first, "glyphs": glyphs,
        "baseline": a_rows[-1] + 1, "cap": a_rows[-1] - a_rows[0] + 1,
    }


def pick_size(font_path, cell_w, cell_h, baseline, cap_target):
    """Largest pixel size whose capitals are not taller than the built-in ones
    and whose glyphs fit the cell (advance, ascender, descender)."""
    best = None
    for size in range(6, 40):
        font = ImageFont.truetype(font_path, size)
        cap_top = font.getbbox("H", anchor="ls")[1]
        cap = -cap_top
        advance = round(font.getlength("M"))
        top = min(font.getbbox(c, anchor="ls")[1] for c in "lbdfhkt|[]()")
        bottom = max(font.getbbox(c, anchor="ls")[3] for c in "gjpqy|[]()_")
        fits = (advance <= cell_w and cap <= cap_target and
                baseline + top >= 0 and baseline + bottom <= cell_h)
        if fits:
            best = size
    if best is None:
        raise SystemExit("no size of %s fits a %dx%d cell" % (font_path, cell_w, cell_h))
    return best


def render_glyph(font, char, cell_w, cell_h, stride, baseline):
    image = Image.new("L", (cell_w, cell_h), 0)
    ImageDraw.Draw(image).text((0, baseline), char, font=font, fill=255, anchor="ls")
    pixels = image.load()
    out = bytearray(stride * cell_h)
    for y in range(cell_h):
        for x in range(cell_w):
            level = (pixels[x, y] * 15 + 127) // 255
            index = y * stride + x // 2
            if x % 2 == 0:
                out[index] |= level << 4
            else:
                out[index] |= level
    return bytes(out)


def build_slot(slot, regular_path, bold_path):
    ref = load_builtin(slot["builtin"])
    cell_w, cell_h, stride = ref["width"], ref["height"], ref["stride"]
    path = bold_path if slot["bold"] else regular_path
    size = pick_size(path, cell_w, cell_h, ref["baseline"], ref["cap"])
    font = ImageFont.truetype(path, size)
    advance = max(1, min(cell_w, round(font.getlength("M"))))

    widths = [0] * 128
    glyphs = {}
    # Icons below the space come from the built-in font (same cell size).
    for code in range(ref["first"], 32):
        if ref["widths"][code]:
            widths[code] = ref["widths"][code]
            glyphs[code] = ref["glyphs"][code]
    for code in range(32, 127):
        widths[code] = advance
        glyphs[code] = render_glyph(font, chr(code), cell_w, cell_h, stride, ref["baseline"])
    widths[COPYRIGHT_CODE] = advance
    glyphs[COPYRIGHT_CODE] = render_glyph(font, "©", cell_w, cell_h, stride, ref["baseline"])

    first = min(glyphs)
    missing = [c for c in range(first, 128) if c not in glyphs]
    if missing:
        # The chip finds a glyph by its code, so the range must have no holes.
        for code in missing:
            widths[code] = 1
            glyphs[code] = bytes(stride * cell_h)

    block = bytearray(widths)
    block += struct.pack("<5I", L4, stride, cell_w, cell_h, 0)
    for code in range(first, 128):
        block += glyphs[code]
    print("  %-10s %s size %2d  cell %dx%d  advance %d  chars %d-127  %d bytes" % (
        slot["name"], os.path.basename(path), size, cell_w, cell_h, advance, first, len(block)))
    return bytes(block), (cell_w, cell_h, stride, first, glyphs, widths)


def build_daw_slot(slot, regular_path, bold_path):
    path = bold_path if slot["bold"] else regular_path
    advance = slot["advance"]
    size = max(s for s in range(5, 60)
               if round(ImageFont.truetype(path, s).getlength("M")) <= advance)
    font = ImageFont.truetype(path, size)
    chars = [chr(c) for c in range(32, 127)] + ["\u00a9"]
    cap = -font.getbbox("H", anchor="ls")[1]
    ascent = max(-font.getbbox(c, anchor="ls")[1] for c in chars)
    descent = max(font.getbbox(c, anchor="ls")[3] for c in chars)
    baseline = max(ascent, cap + descent)
    cell_h = 2 * baseline - cap
    cell_w = advance
    stride = (cell_w + 1) // 2

    widths = [0] * 128
    glyphs = {}
    for code in range(32, 127):
        widths[code] = advance
        glyphs[code] = render_glyph(font, chr(code), cell_w, cell_h, stride, baseline)
    widths[COPYRIGHT_CODE] = advance
    glyphs[COPYRIGHT_CODE] = render_glyph(font, "\u00a9", cell_w, cell_h, stride, baseline)

    block = bytearray(widths)
    block += struct.pack("<5I", L4, stride, cell_w, cell_h, 0)
    for code in range(32, 128):
        block += glyphs[code]
    print("  %-10s %s size %2d  cell %dx%d  cap %d  baseline %d  %d bytes" % (
        slot["name"], os.path.basename(path), size, cell_w, cell_h, cap, baseline, len(block)))
    return bytes(block), (cell_w, cell_h, stride, 32, glyphs, widths)


def write_preview(path, slots):
    sample = ["The quick brown fox jumps over the lazy dog 0123456789",
              "Don't push, Track.  > help  Params: Mixer  ©2026"]
    rows = []
    for cell_w, cell_h, stride, first, glyphs, widths in slots:
        for line in sample:
            rows.append((line, cell_w, cell_h, stride, glyphs, widths))
    width = max(len(r[0]) * r[1] for r in rows) + 20
    height = sum(r[2] + 4 for r in rows) + 20
    image = Image.new("L", (width, height), 20)
    y = 10
    for line, cell_w, cell_h, stride, glyphs, widths in rows:
        x = 10
        for ch in line:
            code = COPYRIGHT_CODE if ch == "©" else ord(ch)
            g = glyphs.get(code)
            if g:
                for gy in range(cell_h):
                    for gx in range(cell_w):
                        b = g[gy * stride + gx // 2]
                        level = (b >> 4) if gx % 2 == 0 else (b & 15)
                        if level:
                            image.putpixel((x + gx, y + gy), 20 + level * 15)
            x += widths[code] if code < 128 else cell_w
        y += cell_h + 4
    image.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("regular")
    parser.add_argument("bold")
    parser.add_argument("output")
    parser.add_argument("--preview")
    parser.add_argument("--layout", choices=["global", "daw"], default="global",
                        help="global: Roboto Mono cell sizes for the whole UI; "
                             "daw: the DAW Control screen font")
    args = parser.parse_args()
    slots = DAW_SLOTS if args.layout == "daw" else SLOTS
    build = build_daw_slot if args.layout == "daw" else build_slot

    blocks = []
    previews = []
    print("Building %s" % args.output)
    for slot in slots:
        block, preview = build(slot, args.regular, args.bold)
        if len(block) > 22396:
            raise SystemExit("slot %s is %d bytes; the Tracker allows 22396" % (slot["name"], len(block)))
        blocks.append(block)
        previews.append(preview)

    header = b"PTFN" + struct.pack("<HH", 1, len(slots)) + bytes(8)
    table_size = len(slots) * 12
    offset = len(header) + table_size
    table = b""
    for i, block in enumerate(blocks):
        table += struct.pack("<IIHH", offset, len(block), i + 1, L4)
        offset += len(block)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(header + table + b"".join(blocks))
    print("  total %d bytes" % offset)
    if args.preview:
        write_preview(args.preview, previews)
        print("Preview: %s" % args.preview)


if __name__ == "__main__":
    sys.exit(main())
