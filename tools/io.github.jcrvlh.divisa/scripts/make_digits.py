#!/usr/bin/env python3
"""
make_digits.py — gera src/digits_rot.h: os dígitos 0-9 do kit_display_72 e o
"%" do kit_display_44, girados 180°, como imagens LVGL A8.

Por quê: o jogador de cima vê a tela de cabeça pra baixo, e a rotação de
objeto (transform_rotation) não está na tabela de símbolos das Tools. Os
glifos saem dos próprios .c das fontes do firmware (bitmap 4 bpp, sem
compressão), então o número invertido é o MESMO desenho do número normal.

Cada imagem é a célula inteira do glifo (avanço x altura da linha), pra que
uma fileira de imagens alinhada pelo topo espelhe exatamente uma fileira de
labels alinhada por baixo.

    python3 scripts/make_digits.py [pasta kit_fonts/src]
    (padrão: ~/Projetos/kit/firmware/components/kit_fonts/src)
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
DEFAULT_SRC = Path.home() / "Projetos/kit/firmware/components/kit_fonts/src"


def load_font(path: Path):
    t = path.read_text(encoding="utf-8")
    m = re.search(r"glyph_bitmap\[\]\s*=\s*\{(.*?)\};", t, re.S)
    bitmap = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", m.group(1))]
    dsc = [tuple(int(v) for v in g) for g in re.findall(
        r"\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
        r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)", t)]
    cmap = {}
    for rs, rl, gs, ul in re.findall(
            r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*"
            r"\.unicode_list = (\w+)", t):
        rs, rl, gs = int(rs), int(rl), int(gs)
        if ul == "NULL":
            for i in range(rl):
                cmap[rs + i] = gs + i
        else:
            arr = re.search(ul + r"\[\] = \{([^}]*)\}", t).group(1)
            for i, o in enumerate(int(x, 0) for x in arr.replace("\n", " ").split(",") if x.strip()):
                cmap[rs + o] = gs + i
    bpp = int(re.search(r"\.bpp = (\d+)", t).group(1))
    assert bpp == 4 and re.search(r"\.bitmap_format = 0", t), "esperado 4 bpp sem compressão"
    line_h = int(re.search(r"\.line_height = (\d+)", t).group(1))
    base = int(re.search(r"\.base_line = (\d+)", t).group(1))
    return bitmap, dsc, cmap, line_h, base


def glyph_cell(font, ch):
    """Célula A8 (lista de linhas) do glifo `ch`, do jeito que o LVGL desenha."""
    bitmap, dsc, cmap, line_h, base = font
    gid = cmap[ord(ch)]
    bi, adv, bw, bh, ox, oy = dsc[gid]
    w = (adv + 8) >> 4
    cell = [[0] * w for _ in range(line_h)]
    top = line_h - base - bh - oy
    for i in range(bw * bh):
        byte = bitmap[bi + (i >> 1)]
        v = (byte >> 4) if (i & 1) == 0 else (byte & 0xF)
        x, y = ox + i % bw, top + i // bw
        if 0 <= x < w and 0 <= y < line_h:
            cell[y][x] = v * 17
    return cell


def rot180(cell):
    return [list(reversed(row)) for row in reversed(cell)]


def emit(name, cell):
    h, w = len(cell), len(cell[0])
    flat = [v for row in cell for v in row]
    lines = []
    for i in range(0, len(flat), 24):
        lines.append("    " + ", ".join(str(v) for v in flat[i:i + 24]) + ",")
    return (f"static const uint8_t {name}_map[{len(flat)}] = {{\n" + "\n".join(lines) + "\n};\n"
            f"static const lv_image_dsc_t {name} = {{\n"
            f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
            f"    .header.cf = LV_COLOR_FORMAT_A8,\n"
            f"    .header.flags = 0,\n"
            f"    .header.w = {w},\n"
            f"    .header.h = {h},\n"
            f"    .header.stride = {w},\n"
            f"    .data_size = {len(flat)},\n"
            f"    .data = {name}_map,\n"
            f"}};\n")


def main(argv):
    src = Path(argv[1]) if len(argv) > 1 else DEFAULT_SRC
    f72 = load_font(src / "kit_display_72.c")
    f44 = load_font(src / "kit_display_44.c")
    out = ["/* GERADO por scripts/make_digits.py — não editar à mão.",
           " * Dígitos do kit_display_72 e '%' do kit_display_44 girados 180°, pro",
           " * jogador de cima ler o placar. Célula inteira de cada glifo. */",
           "#pragma once", '#include "kit_lvgl.h"', ""]
    total = 0
    for d in "0123456789":
        c = rot180(glyph_cell(f72, d))
        total += len(c) * len(c[0])
        out.append(emit(f"digit_rot_{d}", c))
    c = rot180(glyph_cell(f44, "%"))
    total += len(c) * len(c[0])
    out.append(emit("digit_rot_pct", c))
    out.append("static const lv_image_dsc_t *const DIGIT_ROT[10] = {\n    " +
               ", ".join(f"&digit_rot_{d}" for d in "0123456789") + ",\n};\n")
    dst = TOOL_DIR / "src" / "digits_rot.h"
    dst.write_text("\n".join(out), encoding="utf-8")
    print(f"{dst.relative_to(TOOL_DIR)}: 11 imagens, {total} bytes de bitmap")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
