#!/usr/bin/env python3
"""Converts a TTF font into the VLW font format (Processing PFont),
which M5GFX/LovyanGFX can load via display.loadFont() (anti-aliasing,
8-bit grayscale bitmaps per glyph instead of 1-bit pixels like the
built-in bitmap fonts).

Format (big-endian), see M5GFX src/lgfx/v1/lgfx_fonts.cpp VLWfont::loadFont():

Header (24 bytes):
  uint32 gCount       number of glyphs
  uint32 version      (always 11)
  uint32 fontSize      original point size (fallback info only)
  uint32 reserved      (0)
  uint32 ascent
  uint32 descent

Per glyph (28 bytes):
  uint32 unicode
  uint32 height        bitmap height in pixels
  uint32 width         bitmap width in pixels
  uint32 xAdvance
  int32  dY            y distance from the baseline to the top of the bitmap
  int32  dX            x distance from the cursor to the left edge of the bitmap
  uint32 reserved      (0)

After that: for each glyph, width*height bytes of alpha (0-255), row by row.
"""
import struct
import sys
from PIL import ImageFont


def build_vlw(font_path, font_index, point_size, chars, out_path):
    font = ImageFont.truetype(font_path, point_size, index=font_index)
    ascent, descent = font.getmetrics()

    # gUnicode must be strictly ascending - VLWfont::getUnicodeIndex() does
    # a std::lower_bound (binary search) on it.
    chars = sorted(set(chars), key=ord)

    glyphs = []
    for ch in chars:
        # getmask returns an 8-bit grayscale bitmap (alpha) of the glyph,
        # already anti-aliased by FreeType.
        mask = font.getmask(ch, mode="L")
        w, h = mask.size
        bbox = font.getbbox(ch)
        xAdvance = font.getlength(ch)
        if bbox is None:
            dX, dY = 0, ascent
            w, h = 0, 0
            pixels = b""
        else:
            left, top, right, bottom = bbox
            dX = left
            dY = ascent - top
            pixels = bytes(mask) if w and h else b""
        glyphs.append({
            "unicode": ord(ch),
            "height": h,
            "width": w,
            "xAdvance": int(round(xAdvance)),
            "dY": dY,
            "dX": dX,
            "pixels": pixels,
        })

    with open(out_path, "wb") as f:
        f.write(struct.pack(">IIIIII", len(glyphs), 11, point_size, 0, ascent, descent))
        for g in glyphs:
            f.write(struct.pack(">IIIIiiI", g["unicode"], g["height"], g["width"],
                                 g["xAdvance"], g["dY"], g["dX"], 0))
        for g in glyphs:
            f.write(g["pixels"])

    print(f"{out_path}: {len(glyphs)} glyphs, {point_size}pt, "
          f"{sum(len(g['pixels']) for g in glyphs)} bytes of bitmap data")


if __name__ == "__main__":
    FONT = "/System/Library/Fonts/HelveticaNeue.ttc"
    BOLD_INDEX = 1  # 'Helvetica Neue Bold', see sample above

    ascii_charset = "".join(chr(c) for c in range(0x20, 0x7F))
    umlaute = "ÄÖÜäöüß"

    # UI font: prominent texts (project/activity name).
    build_vlw(FONT, BOLD_INDEX, 22, ascii_charset + umlaute,
              "/Users/christiangrewenig/Documents/git/kimai-dial/data/ui_bold_22.vlw")

    # Smaller variant of the same font for titles/hints/indicator - same
    # font as ui_bold_22, just baked smaller so that, e.g.,
    # "Drehen = waehlen, Klick = oeffnen" fits on the screen.
    build_vlw(FONT, BOLD_INDEX, 14, ascii_charset + umlaute,
              "/Users/christiangrewenig/Documents/git/kimai-dial/data/ui_bold_14.vlw")

    # Clock font: digits + colon only, but much larger.
    # 50pt chosen because "00:00:00" comes out to about 197px wide with it,
    # which fits on the 240px round display with a margin (64pt would be
    # too wide at 252px).
    build_vlw(FONT, BOLD_INDEX, 50, "0123456789:",
              "/Users/christiangrewenig/Documents/git/kimai-dial/data/clock_bold_50.vlw")
