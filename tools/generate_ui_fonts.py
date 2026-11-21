"""Generate the project's LVGL 4-bpp fonts from the Chinese UI text.

Requires Python 3 + Pillow (FreeType enabled). The complete Source Han Sans
font and its SIL OFL 1.1 license are included in the LVGL dependency.
Run again after adding UI text; generated C files are committed so normal
ESP-IDF builds do not require Pillow or a font conversion tool.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "managed_components/lvgl__lvgl/scripts/generators/built_in_font"


def font_codepoints(path: Path) -> set[int]:
    """Read the font's Unicode cmap to reject missing glyphs before export."""
    data = path.read_bytes()
    table_count = struct.unpack_from(">H", data, 4)[0]
    cmap = None
    for index in range(table_count):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + index * 16)
        if tag == b"cmap":
            cmap = offset
            break
    if cmap is None:
        raise ValueError("The font has no cmap table")
    covered: set[int] = set()
    _, count = struct.unpack_from(">HH", data, cmap)
    for index in range(count):
        platform, encoding, relative = struct.unpack_from(">HHI", data, cmap + 4 + index * 8)
        if platform not in (0, 3):
            continue
        pos = cmap + relative
        fmt = struct.unpack_from(">H", data, pos)[0]
        if fmt == 12:
            groups = struct.unpack_from(">I", data, pos + 12)[0]
            for group in range(groups):
                start, end, first_glyph = struct.unpack_from(">III", data, pos + 16 + group * 12)
                covered.update(range(start + (first_glyph == 0), end + 1))
        elif fmt == 4:
            seg_count = struct.unpack_from(">H", data, pos + 6)[0] // 2
            end_base = pos + 14
            start_base = end_base + seg_count * 2 + 2
            delta_base = start_base + seg_count * 2
            range_base = delta_base + seg_count * 2
            for segment in range(seg_count):
                end = struct.unpack_from(">H", data, end_base + segment * 2)[0]
                start = struct.unpack_from(">H", data, start_base + segment * 2)[0]
                delta = struct.unpack_from(">h", data, delta_base + segment * 2)[0]
                range_offset = struct.unpack_from(">H", data, range_base + segment * 2)[0]
                for code in range(start, min(end, 0xFFFE) + 1):
                    if range_offset:
                        address = range_base + segment * 2 + range_offset + (code - start) * 2
                        glyph = struct.unpack_from(">H", data, address)[0]
                        glyph = ((glyph + delta) & 0xFFFF) if glyph else 0
                    else:
                        glyph = (code + delta) & 0xFFFF
                    if glyph:
                        covered.add(code)
    return covered


def ui_characters(path: Path) -> list[int]:
    source = path.read_text(encoding="utf-8")
    literals = re.findall(r'"(?:\\.|[^"\\])*"', source)
    text = "".join(json.loads(literal) for literal in literals)
    codes = set(range(32, 127))
    codes.update(ord(char) for char in text if ord(char) >= 32)
    if max(codes) - min(codes) >= 65535:
        raise ValueError("Characters need multiple LVGL cmap ranges")
    return sorted(codes)


def values(data: list[int] | bytes, hexadecimal: bool = True) -> str:
    def item(number: int) -> str:
        return f"0x{number:02x}" if hexadecimal else str(number)
    return "\n".join("    " + ", ".join(item(v) for v in data[i:i + 16]) + ","
                     for i in range(0, len(data), 16))


def generate(font_path: Path, codes: list[int], size: int, output: Path) -> dict:
    font = ImageFont.truetype(str(font_path), size)
    bitmap = bytearray()
    descriptors = ["    { .bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0 },"]
    max_ascent = max_descent = 0
    for code in codes:
        char = chr(code)
        mask, offset = font.getmask2(char, mode="L", anchor="ls")
        image = Image.frombytes("L", mask.size, bytes(mask))
        bbox = image.getbbox()
        start = len(bitmap)
        if bbox:
            image = image.crop(bbox)
            width, height = image.size
            x_offset = offset[0] + bbox[0]
            y_offset = -(offset[1] + bbox[3])
            max_ascent = max(max_ascent, height + y_offset)
            max_descent = max(max_descent, -y_offset)
            pixels = [(pixel * 15 + 127) // 255 for pixel in image.tobytes()]
            pixels += [0] * (len(pixels) % 2)
            bitmap.extend((pixels[i] << 4) | pixels[i + 1] for i in range(0, len(pixels), 2))
        else:
            width = height = x_offset = y_offset = 0
        advance = round(font.getlength(char) * 16)
        descriptors.append(f"    {{ .bitmap_index = {start}, .adv_w = {advance}, .box_w = {width}, .box_h = {height}, .ofs_x = {x_offset}, .ofs_y = {y_offset} }}, /* U+{code:04X} */")
    line_height = max(size + 4, max_ascent + max_descent)
    baseline = max_descent
    range_start = codes[0]
    relative = [code - range_start for code in codes]
    code = f'''/* Generated by tools/generate_ui_fonts.py; do not edit.
 * Source: SourceHanSansSC-Normal.otf, Copyright 2014-2021 Adobe.
 * Font data licensed under SIL OFL 1.1; see LICENSE-SourceHanSansSC.txt.
 * {size} px, 4 bpp, no compression, {len(codes)} glyphs.
 */
#include "ui_fonts.h"

static const uint8_t glyph_bitmap[] = {{
{values(bitmap)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{chr(10).join(descriptors)}
}};

static const uint16_t unicode_list[] = {{
{values(relative, False)}
}};

static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{
        .range_start = {range_start}, .range_length = {codes[-1] - range_start + 1},
        .glyph_id_start = 1, .unicode_list = unicode_list,
        .glyph_id_ofs_list = NULL, .list_length = {len(codes)},
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
    }},
}};

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,
    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = 1, .bpp = 4,
    .kern_classes = 0, .bitmap_format = 0,
}};

const lv_font_t ui_font_{size} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height}, .base_line = {baseline},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2, .underline_thickness = 1,
    .static_bitmap = 1, .dsc = &font_dsc,
    .fallback = NULL, .user_data = NULL,
}};
'''
    output.write_text(code, encoding="utf-8", newline="\n")
    return {"size": size, "glyphs": len(codes), "bitmap_bytes": len(bitmap),
            "line_height": line_height, "base_line": baseline}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, default=SOURCE / "SourceHanSansSC-Normal.otf")
    parser.add_argument("--ui", type=Path, default=ROOT / "main/ui.c")
    parser.add_argument("--preview", type=Path, help="Optional PNG showing exported Chinese text")
    args = parser.parse_args()
    codes = ui_characters(args.ui)
    missing = set(codes) - font_codepoints(args.font)
    if missing:
        raise SystemExit("Missing source glyphs: " + ", ".join(f"U+{code:04X}" for code in sorted(missing)))
    output = ROOT / "main/fonts"
    output.mkdir(parents=True, exist_ok=True)
    (output / "ui_fonts.h").write_text('''#pragma once
#include "lvgl.h"

LV_FONT_DECLARE(ui_font_16);
LV_FONT_DECLARE(ui_font_22);
''', encoding="utf-8", newline="\n")
    stats = [generate(args.font, codes, size, output / f"ui_font_{size}.c") for size in (16, 22)]
    license_text = (SOURCE / "font_license/SourceHanSansSC/LICENSE.txt").read_text(encoding="utf-8")
    (output / "LICENSE-SourceHanSansSC.txt").write_text(license_text, encoding="utf-8", newline="\n")
    (output / "ui_font_characters.txt").write_text("".join(map(chr, codes)) + "\n", encoding="utf-8", newline="\n")
    if args.preview:
        chinese = "".join(chr(code) for code in codes if code >= 0x3000)
        sample = [chinese[i:i + 24] for i in range(0, len(chinese), 24)]
        preview = Image.new("RGB", (600, 40 + 30 * len(sample)), "#12000f")
        draw = ImageDraw.Draw(preview)
        font = ImageFont.truetype(str(args.font), 22)
        for index, line in enumerate(sample):
            draw.text((20, 14 + index * 30), line, font=font, fill="#fff2fa")
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        preview.save(args.preview)
    print(json.dumps({"source_glyph_coverage": "complete", "fonts": stats}, ensure_ascii=False))


if __name__ == "__main__":
    main()
