"""Generate an OFL Noto Sans atlas and runtime-compatible MFNT metrics externally.

MFNT is a documented layout consumed by engine_mercury.cpp, not a copied game asset.
Requires Pillow and fontTools; the repository's licensed font remains the only input.
"""
import shutil
import struct
from pathlib import Path


def generate(output: Path, repo: Path):
    from PIL import Image, ImageDraw, ImageFont
    from fontTools.ttLib import TTFont

    fonts = repo / "dm/third_party/fonts"
    source = fonts / "NotoSans-Regular-subset.ttf"
    cmap = TTFont(source).getBestCmap()
    font = ImageFont.truetype(str(source), 48)
    # Latin names, punctuation and gender signs. Missing characters use '?'; no
    # strings or outlines from the running game are included in this artifact.
    codes = [cp for cp in cmap if 32 <= cp <= 0x2642]
    atlas = Image.new("RGBA", (2048, 2048))
    draw = ImageDraw.Draw(atlas)
    records = {}
    x = y = row = 1
    for cp in sorted(codes):
        ch = chr(cp)
        left, top, right, bottom = font.getbbox(ch, anchor="ls")
        w, h = right - left, bottom - top
        if x + w + 1 > atlas.width:
            x, y, row = 1, y + row + 2, 1
        if y + h + 1 > atlas.height:
            raise ValueError("Noto atlas exceeded its bounded canvas")
        draw.text((x - left, y - top), ch, font=font, anchor="ls", fill="white")
        records[cp] = struct.pack("<HHHHhhH", x, y, w, h, left, -top,
                                  round(font.getlength(ch)))
        x += w + 2
        row = max(row, h)
    atlas = atlas.crop((0, 0, atlas.width, y + row + 1))
    output.mkdir(parents=True, exist_ok=True)
    atlas.save(output / "noto.png")
    last = max(codes)
    # Header and reserved notdef record; glyph index1 represents U+0020.
    header = bytearray(0x60)
    header[:4] = b"MFNT"
    struct.pack_into("<I", header, 0x1c, -font.getbbox("H", anchor="ls")[1])
    struct.pack_into("<I", header, 0x20, last - 32 + 2)
    struct.pack_into("<I", header, 0x28, len(header))
    fallback = records[ord("?")]
    metrics = bytes(header) + bytes(14) + b"".join(
        records.get(cp, fallback) for cp in range(32, last + 1))
    (output / "noto.mfnt").write_bytes(metrics)
    shutil.copyfile(fonts / "OFL.txt", output / "OFL.txt")
    print(f"Generated licensed Noto font: {len(records)} glyphs, atlas {atlas.size}")
