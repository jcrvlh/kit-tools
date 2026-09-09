#!/usr/bin/env python3
"""
make_shapes.py — gera src/shapes.h: as 3 formas do KIT (círculo, triângulo
equilátero, quadrado) como máscaras A8 (1 byte de alfa por pixel), no mesmo
tamanho de caixa. A Tool cria um lv_image por forma e recolore em runtime.

Os glifos KIT_ICON_* só existem até kit_display_44 e o triângulo (fa caret-up)
não é equilátero — por isso as formas viram bitmap embutido no .so (padrão da
Tool Veto). A8 a 88x88 = ~7,7 KB por forma.

    python3 scripts/make_shapes.py            # regenera src/shapes.h
    python3 scripts/make_shapes.py --check    # falha se está desatualizado
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
OUT = TOOL_DIR / "src" / "shapes.h"

S = 88          # lado da caixa (px) — igual pras 3 formas
SS = 4          # subamostras por eixo (antialias)


def _circle(x, y):
    r = S / 2.0
    return math.hypot(x - r, y - r) <= r - 0.5


def _square(x, y):
    return 0.5 <= x <= S - 0.5 and 0.5 <= y <= S - 0.5


def _triangle(x, y):
    # equilátero de lado S, apontando pra cima, centrado na caixa
    h = S * math.sqrt(3.0) / 2.0
    top = (S - h) / 2.0
    base = top + h
    if y < top or y > base:
        return False
    t = (y - top) / h                 # 0 no ápice, 1 na base
    half = (S / 2.0) * t
    cx = S / 2.0
    return (cx - half) <= x <= (cx + half)


SHAPES = [("circle", _circle), ("triangle", _triangle), ("square", _square)]


def render(fn):
    step = 1.0 / SS
    base = step / 2.0
    inv = 255.0 / (SS * SS)
    out = bytearray(S * S)
    for j in range(S):
        for i in range(S):
            hits = 0
            for sj in range(SS):
                yy = j + base + sj * step
                for si in range(SS):
                    xx = i + base + si * step
                    if fn(xx, yy):
                        hits += 1
            out[j * S + i] = int(round(hits * inv))
    return bytes(out)


def c_array(name, data):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for k in range(0, len(data), 20):
        chunk = ", ".join(str(b) for b in data[k:k + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def c_dsc(name, arr):
    return (
        f"static const lv_image_dsc_t {name} = {{\n"
        f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
        f"    .header.cf = LV_COLOR_FORMAT_A8,\n"
        f"    .header.flags = 0,\n"
        f"    .header.w = {S},\n"
        f"    .header.h = {S},\n"
        f"    .header.stride = {S},\n"
        f"    .data_size = {S * S},\n"
        f"    .data = {arr},\n"
        f"}};"
    )


def build_header():
    parts = [
        "/* GERADO por scripts/make_shapes.py — não editar à mão. */",
        "#pragma once",
        '#include "kit_lvgl.h"',
        "",
        f"#define SHAPE_IMG_SIZE {S}",
        "",
    ]
    dscs = []
    for name, fn in SHAPES:
        data = render(fn)
        parts.append(c_array(f"shape_{name}_map", data))
        parts.append("")
        parts.append(c_dsc(f"shape_{name}_img", f"shape_{name}_map"))
        parts.append("")
        dscs.append(f"&shape_{name}_img")
    parts.append(f"static const lv_image_dsc_t *const SHAPE_IMG[3] = {{ {', '.join(dscs)} }};")
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
    print(f"src/shapes.h  ({S}x{S} A8, 3 formas)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
