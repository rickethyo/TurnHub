#!/usr/bin/env python3
"""Builds the Brass display fonts from Cinzel and Oswald (both SIL Open Font
License 1.1): Cinzel for words, Oswald's lining figures for numbers.

Atlas (LovyanGFX) gets anti-aliased VLW fonts; the Sigils (Adafruit GFX) get
1-bit GFXfont bitmaps. Both land in generated headers:

  Atlas/include/brass_fonts.h   (VLW byte arrays, BrassFonts namespace)
  Sigil/include/brass_fonts.h   (GFXfont structs, BrassFonts namespace)

Usage (from the repository root, with freetype-py installed):

  python tools/fonts/make_fonts.py --ttf-dir <dir with Cinzel-700.ttf, Cinzel-900.ttf, Oswald-600.ttf>
  python tools/fonts/make_fonts.py --ttf-dir <dir> --preview out/   # also writes PNG previews

The TTFs are not checked in: fetch Cinzel's static Bold (700) and Black (900)
and Oswald SemiBold (600) from Google Fonts (or github.com/NDISCOVER/Cinzel and
github.com/googlefonts/OswaldFont). The license text travels with the
generated headers (tools/fonts/OFL.txt), and both fonts are listed in
Documentation/legal/DEPENDENCY_TRACKER.md.
"""

import argparse
import os
import struct
import sys
import zlib

import freetype

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ASCII = "".join(chr(c) for c in range(32, 127))
DIGITS = "".join(chr(c) for c in range(0x2B, 0x3B))  # + , - . / 0-9 :

# name, face (TTF file name), pixel size, characters, what uses it
ATLAS_FONTS = [
    ("Title", "Cinzel-700", 22, ASCII, "hero titles"),
    ("Label", "Cinzel-700", 16, ASCII, "button labels, header badge"),
    ("Name", "Cinzel-700", 13, ASCII, "player chip names, header text"),
    ("Tag", "Cinzel-700", 11, ASCII, "chip tags, small captions"),
    ("Numerals", "Oswald-600", 30, DIGITS, "life totals"),
    ("Clock", "Oswald-600", 15, DIGITS, "the gauge's clock"),
    ("Code", "Oswald-600", 44, DIGITS + " ", "presence code digits"),
    ("Wordmark", "Cinzel-900", 34, " ABHNRTU", "splash wordmark TURNHUB"),
]

SIGIL_FONTS = [
    ("EinkTitle", "Cinzel-900", 17, ASCII, "e-ink header title"),
    ("EinkBanner", "Cinzel-900", 14, ASCII, "e-ink turn banner"),
    ("EinkName", "Cinzel-700", 18, ASCII, "e-ink player name"),
    ("EinkSmall", "Cinzel-700", 11, ASCII, "e-ink small caps labels"),
    ("EinkLife", "Oswald-600", 50, DIGITS, "e-ink life total"),
    ("EinkLifeSmall", "Oswald-600", 32, DIGITS, "e-ink life total, shared seat"),
    ("OledSmall", "Cinzel-700", 10, ASCII, "OLED player names and small Cinzel lines"),
    ("OledHeader", "Cinzel-900", 10, ASCII, "OLED header title (caps fit the 11 px bar)"),
    ("OledName", "Cinzel-700", 15, ASCII, "OLED big lines: seat name, Sigil number, status word"),
    ("OledLife", "Oswald-600", 28, DIGITS, "OLED life total, single seat"),
    ("OledLifeMid", "Oswald-600", 22, DIGITS, "OLED life total when 28 px is too wide"),
    ("OledLifeSmall", "Oswald-600", 18, DIGITS, "OLED life total: shared seat, commander rows shown"),
]

# 1-bit threshold for the Sigil fonts (0-255 coverage). Slightly under half
# keeps Cinzel's thin serifs on the page.
MONO_THRESHOLD = 100


def load_face(ttf_dir, face_name):
    path = os.path.join(ttf_dir, f"{face_name}.ttf")
    if not os.path.exists(path):
        sys.exit(f"missing {path}")
    return freetype.Face(path)


def render(face, size, ch):
    """Grayscale glyph: (width, height, left, top, advance, rows of 0-255)."""
    face.set_pixel_sizes(0, size)
    face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
    g = face.glyph
    bm = g.bitmap
    rows = [list(bm.buffer[r * bm.pitch:r * bm.pitch + bm.width]) for r in range(bm.rows)]
    advance = (g.advance.x + 32) >> 6
    return bm.width, bm.rows, g.bitmap_left, g.bitmap_top, advance, rows


def metrics(face, size):
    face.set_pixel_sizes(0, size)
    m = face.size
    return (m.ascender + 32) >> 6, (-m.descender + 32) >> 6


def vlw_font(face, size, chars):
    """Processing VLW: big-endian header, 7-word glyph records, 8-bit bitmaps."""
    ascent, descent = metrics(face, size)
    # LovyanGFX looks glyphs up by binary search: keep the table sorted.
    glyphs = [render(face, size, ch) + (ord(ch),) for ch in sorted(set(chars))]
    out = bytearray(struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent))
    for w, h, left, top, adv, _, code in glyphs:
        if code == 0x20:
            w = h = 0
        out += struct.pack(">7i", code, h, w, adv, top, left, 0)
    for w, h, _, _, _, rows, code in glyphs:
        if code == 0x20:
            continue
        for row in rows:
            out += bytes(row)
    return bytes(out)


def gfx_font(face, size, chars):
    """Adafruit GFXfont: packed 1-bit bitmaps and glyph records."""
    first, last = ord(chars[0]), ord(chars[-1])
    assert "".join(chr(c) for c in range(first, last + 1)) == chars, "GFX fonts need a contiguous range"
    ascent, descent = metrics(face, size)
    bits = []
    glyphs = []
    for ch in chars:
        w, h, left, top, adv, rows = render(face, size, ch)
        if ch == " ":
            w = h = 0
            rows = []
        offset = len(bits) // 8
        for row in rows:
            bits.extend(1 if v >= MONO_THRESHOLD else 0 for v in row)
        while len(bits) % 8:
            bits.append(0)
        glyphs.append((offset, w, h, adv, left, -top))
    data = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for b in bits[i:i + 8]:
            byte = (byte << 1) | b
        data.append(byte)
    return bytes(data), glyphs, first, last, ascent + descent


def c_bytes(data, indent="  "):
    lines = []
    for i in range(0, len(data), 16):
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    return "\n".join(lines)


HEADER_NOTE = """// GENERATED by tools/fonts/make_fonts.py: do not edit by hand.
// Bitmaps rendered for the Brass display theme from:
//   Cinzel, Copyright 2020 The Cinzel Project Authors
//   (https://github.com/NDISCOVER/Cinzel)
//   Oswald, Copyright 2016 The Oswald Project Authors
//   (https://github.com/googlefonts/OswaldFont)
// both licensed under the SIL Open Font License 1.1 (tools/fonts/OFL.txt)."""


def write_atlas(ttf_dir, filename="brass_fonts.h", namespace="BrassFonts"):
    parts = [HEADER_NOTE, "", "#pragma once", "", "#include <stddef.h>", "#include <stdint.h>", "",
             f"namespace {namespace} {{", ""]
    total = 0
    for name, weight, size, chars, use in ATLAS_FONTS:
        data = vlw_font(load_face(ttf_dir, weight), size, chars)
        total += len(data)
        parts.append(f"// {name}: {weight}, {size} px, {len(chars)} glyphs ({use}).")
        parts.append(f"alignas(4) const uint8_t {name}Vlw[] = {{")
        parts.append(c_bytes(data))
        parts.append("};")
        parts.append(f"constexpr size_t {name}VlwSize = sizeof({name}Vlw);")
        parts.append("")
    parts.append(f"}}  // namespace {namespace}")
    path = os.path.join(ROOT, "Atlas", "include", filename)
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(parts) + "\n")
    print(f"{path}: {total} bytes of font data")


def write_sigil(ttf_dir, filename="brass_fonts.h", namespace="BrassFonts"):
    parts = [HEADER_NOTE, "", "#pragma once", "", "#include <gfxfont.h>", "", f"namespace {namespace} {{", ""]
    total = 0
    for name, weight, size, chars, use in SIGIL_FONTS:
        data, glyphs, first, last, y_advance = gfx_font(load_face(ttf_dir, weight), size, chars)
        total += len(data) + 7 * len(glyphs)
        parts.append(f"// {name}: {weight}, {size} px, 1-bit ({use}).")
        parts.append(f"const uint8_t {name}Bitmaps[] = {{")
        parts.append(c_bytes(data) if data else "  0x00,")
        parts.append("};")
        parts.append(f"const GFXglyph {name}Glyphs[] = {{")
        for (offset, w, h, adv, x_off, y_off), code in zip(glyphs, range(first, last + 1)):
            label = chr(code) if chr(code) not in "\\'" else "\\" + chr(code)
            parts.append(f"  {{{offset}, {w}, {h}, {adv}, {x_off}, {y_off}}},  // '{label}'")
        parts.append("};")
        parts.append(f"const GFXfont {name} = {{const_cast<uint8_t *>({name}Bitmaps), "
                     f"const_cast<GFXglyph *>({name}Glyphs), 0x{first:02X}, 0x{last:02X}, {y_advance}}};")
        parts.append("")
    parts.append(f"}}  // namespace {namespace}")
    path = os.path.join(ROOT, "Sigil", "include", filename)
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(parts) + "\n")
    print(f"{path}: {total} bytes of font data")


def write_png(path, pixels, width, height):
    raw = b"".join(b"\x00" + bytes(pixels[y * width:(y + 1) * width]) for y in range(height))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def preview(ttf_dir, out_dir, sample="Rowan's turn  YOUR TURN  Game 0123456789"):
    os.makedirs(out_dir, exist_ok=True)
    for name, weight, size, chars, _ in ATLAS_FONTS + SIGIL_FONTS:
        face = load_face(ttf_dir, weight)
        text = "".join(c for c in sample if c in chars) or chars
        mono = (name, weight, size, chars, _) in SIGIL_FONTS
        ascent, descent = metrics(face, size)
        width = sum(render(face, size, c)[4] for c in text) + 8
        height = ascent + descent + 8
        pix = [255] * (width * height)
        x = 4
        for c in text:
            w, h, left, top, adv, rows = render(face, size, c)
            for r, row in enumerate(rows):
                for col, v in enumerate(row):
                    px, py = x + left + col, 4 + ascent - top + r
                    if 0 <= px < width and 0 <= py < height:
                        ink = (0 if v >= MONO_THRESHOLD else 255) if mono else 255 - v
                        pix[py * width + px] = min(pix[py * width + px], ink)
            x += adv
        scale = 3
        big = []
        for y in range(height * scale):
            for xx in range(width * scale):
                big.append(pix[(y // scale) * width + xx // scale])
        write_png(os.path.join(out_dir, f"{name}.png"), big, width * scale, height * scale)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ttf-dir", required=True)
    parser.add_argument("--preview", help="write enlarged PNG previews here")
    args = parser.parse_args()
    write_atlas(args.ttf_dir)
    write_sigil(args.ttf_dir)
    if args.preview:
        preview(args.ttf_dir, args.preview)


if __name__ == "__main__":
    main()
