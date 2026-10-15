#!/usr/bin/env python3
"""
make_shapes.py — gera src/shapes.h: o triângulo equilátero do KIT como
máscara A8 (1 byte de alfa por pixel) nos tamanhos fixos que a arte das
figurinhas usa, mais a "alça" do envelope (triângulo apontando pra direita).

Círculo e quadrado não precisam de bitmap: saem de um lv_obj com raio. O
triângulo do Font Awesome é um caret (não equilátero) e não existe acima de
kit_display_44 — por isso vira bitmap embutido (padrão Veto/Repete). A caixa é
cortada na altura do triângulo pra não gastar bytes à toa.

    python3 scripts/make_shapes.py            # regenera src/shapes.h
    python3 scripts/make_shapes.py --check    # falha se está desatualizado
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
OUT = TOOL_DIR / "src" / "shapes.h"

SS = 4  # subamostras por eixo (antialias)

# lado do triângulo (px): figurinha pequena 96 -> 52/22, grande 192 -> 104/44
TRI_SIZES = [22, 44, 52, 104]
GRIP = 28


def tri_up(s):
    h = s * math.sqrt(3.0) / 2.0

    def fn(x, y):
        if y < 0 or y > h:
            return False
        half = (s / 2.0) * (y / h)
        return (s / 2.0 - half) <= x <= (s / 2.0 + half)

    return s, int(math.ceil(h)), fn


def tri_right(s):
    w = s * math.sqrt(3.0) / 2.0

    def fn(x, y):
        if x < 0 or x > w:
            return False
        half = (s / 2.0) * (1.0 - x / w)
        return (s / 2.0 - half) <= y <= (s / 2.0 + half)

    return int(math.ceil(w)), s, fn


def render(w, h, fn):
    step = 1.0 / SS
    base = step / 2.0
    inv = 255.0 / (SS * SS)
    out = bytearray(w * h)
    for j in range(h):
        for i in range(w):
            hits = 0
            for sj in range(SS):
                for si in range(SS):
                    if fn(i + base + si * step, j + base + sj * step):
                        hits += 1
            out[j * w + i] = int(round(hits * inv))
    return bytes(out)


def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for k in range(0, len(data), 24):
        lines.append("    " + ", ".join(str(b) for b in data[k:k + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_dsc(name, arr, w, h):
    return (
        f"static const lv_image_dsc_t {name} = {{\n"
        f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
        f"    .header.cf = LV_COLOR_FORMAT_A8,\n"
        f"    .header.flags = 0,\n"
        f"    .header.w = {w},\n"
        f"    .header.h = {h},\n"
        f"    .header.stride = {w},\n"
        f"    .data_size = {w * h},\n"
        f"    .data = {arr},\n"
        f"}};"
    )


def build_header():
    parts = [
        "/* GERADO por scripts/make_shapes.py — não editar à mão. */",
        "#pragma once",
        '#include "kit_lvgl.h"',
        "",
    ]
    entries = []
    for s in TRI_SIZES:
        w, h, fn = tri_up(s)
        parts += [c_array(f"tri_{s}_map", render(w, h, fn)), "",
                  c_dsc(f"tri_{s}_img", f"tri_{s}_map", w, h), ""]
        entries.append((s, h))
    w, h, fn = tri_right(GRIP)
    parts += [c_array("grip_map", render(w, h, fn)), "",
              c_dsc("grip_img", "grip_map", w, h), ""]
    parts.append(f"#define TRI_SIZES {len(TRI_SIZES)}")
    parts.append("static const uint8_t TRI_SIDE[TRI_SIZES] = { " +
                 ", ".join(str(s) for s, _ in entries) + " };")
    parts.append("static const uint8_t TRI_H[TRI_SIZES] = { " +
                 ", ".join(str(h) for _, h in entries) + " };")
    parts.append("static const lv_image_dsc_t *const TRI_IMG[TRI_SIZES] = { " +
                 ", ".join(f"&tri_{s}_img" for s, _ in entries) + " };")
    parts.append(f"#define GRIP_W {w}")
    parts.append(f"#define GRIP_H {h}")
    parts.append("")
    return "\n".join(parts)


def main(argv):
    header = build_header()
    if "--check" in argv:
        cur = OUT.read_text() if OUT.exists() else ""
        if cur != header:
            print("desatualizado: src/shapes.h", file=sys.stderr)
            return 1
        print("src/shapes.h ok")
        return 0
    OUT.write_text(header)
    print(f"src/shapes.h  (triângulos {TRI_SIZES} + alça {GRIP}, A8)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
