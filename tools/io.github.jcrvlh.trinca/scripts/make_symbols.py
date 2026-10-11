#!/usr/bin/env python3
"""
make_symbols.py — gera src/symbols.h: os 7 símbolos dos rolos como máscaras A8
(1 byte de alfa por pixel), todos na mesma caixa. A Tool cria um lv_image por
casa do rolo e recolore em runtime com a cor do símbolo.

Ordem = índice do símbolo em trinca_game.h:
  0 círculo  1 triângulo  2 quadrado  3 losango  4 cruz  5 anel  6 estrela (rara)

As 3 primeiras são as formas do KIT; os glifos KIT_ICON_* só existem até
kit_display_44 e o triângulo da fonte não é equilátero — por isso bitmap
embutido no .so (padrão da Repete/Veto). A8 64x64 = 4 KB por símbolo.

    python3 scripts/make_symbols.py            # regenera src/symbols.h
    python3 scripts/make_symbols.py --check    # falha se está desatualizado
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
OUT = TOOL_DIR / "src" / "symbols.h"

S = 64          # lado da caixa (px)
SS = 4          # subamostras por eixo (antialias)
C = S / 2.0


def _circle(x, y):
    return math.hypot(x - C, y - C) <= C - 1.0


def _triangle(x, y):
    # equilátero apontando pra cima, centrado na caixa pelo centroide visual
    side = S - 2.0
    h = side * math.sqrt(3.0) / 2.0
    top = (S - h) / 2.0
    if y < top or y > top + h:
        return False
    half = (side / 2.0) * (y - top) / h
    return (C - half) <= x <= (C + half)


def _square(x, y):
    # um tico menor que a caixa: quadrado cheio "pesa" mais que o círculo
    m = S * 0.09
    return m <= x <= S - m and m <= y <= S - m


def _diamond(x, y):
    return abs(x - C) + abs(y - C) <= C - 1.0


def _cross(x, y):
    arm = S * 0.15       # meia-espessura do braço
    reach = C - 2.0
    dx, dy = abs(x - C), abs(y - C)
    return (dx <= arm and dy <= reach) or (dy <= arm and dx <= reach)


def _ring(x, y):
    d = math.hypot(x - C, y - C)
    return C * 0.52 <= d <= C - 1.0


def _star(x, y):
    # estrela de 5 pontas, ponta pra cima; testa o ponto no polígono de 10 vértices
    R, r = C - 1.0, (C - 1.0) * 0.45
    cy = C + 2.5          # desce um tico: o centroide visual da estrela é alto
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5
        rad = R if k % 2 == 0 else r
        pts.append((C + rad * math.cos(a), cy + rad * math.sin(a)))
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


SYMBOLS = [
    ("circle", _circle),
    ("triangle", _triangle),
    ("square", _square),
    ("diamond", _diamond),
    ("cross", _cross),
    ("ring", _ring),
    ("star", _star),
]


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
        "/* GERADO por scripts/make_symbols.py — não editar à mão. */",
        "#pragma once",
        '#include "kit_lvgl.h"',
        "",
        f"#define SYM_IMG_SIZE {S}",
        "",
        "#ifndef KIT_SDK_STUBS   /* no build nativo a UI não existe */",
        "",
    ]
    dscs = []
    for name, fn in SYMBOLS:
        parts.append(c_array(f"sym_{name}_map", render(fn)))
        parts.append("")
        parts.append(c_dsc(f"sym_{name}_img", f"sym_{name}_map"))
        parts.append("")
        dscs.append(f"&sym_{name}_img")
    parts.append(f"static const lv_image_dsc_t *const SYM_IMG[{len(SYMBOLS)}] = {{")
    for d in dscs:
        parts.append(f"    {d},")
    parts.append("};")
    parts.append("")
    parts.append("#endif")
    parts.append("")
    return "\n".join(parts)


def main(argv):
    header = build_header()
    if "--check" in argv:
        cur = OUT.read_text() if OUT.exists() else ""
        if cur != header:
            print("desatualizado: src/symbols.h", file=sys.stderr)
            return 1
        print("src/symbols.h ok")
        return 0
    OUT.write_text(header)
    print(f"src/symbols.h  ({S}x{S} A8, {len(SYMBOLS)} símbolos)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
