#!/usr/bin/env python3
"""
make_icon.py — ícone da Tool Repete: as 3 formas do KIT empilhadas na
diagonal (círculo azul, triângulo amarelo, quadrado vermelho).

  icon.png   240x240 RGBA   — catálogo web / previews
  icon.bin    64x64  LVGL v9 (ARGB8888) — asset do pacote .kit

Sem dependências. Mesmo encoder da Telefonema.

    python3 scripts/make_icon.py
    python3 scripts/make_icon.py --check
"""
from __future__ import annotations

import math
import struct
import sys
import zlib
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
DESIGN = 240.0
SS = 4

BLUE = (0x2C, 0x3C, 0xC4, 255)
YELLOW = (0xE9, 0xB2, 0x3C, 255)
RED = (0xC6, 0x47, 0x2F, 255)

R = 46.0  # "raio" nominal de cada forma no espaço 240


def _circle(px, py, cx, cy):
    return math.hypot(px - cx, py - cy) <= R


def _square(px, py, cx, cy):
    return abs(px - cx) <= R and abs(py - cy) <= R


def _triangle(px, py, cx, cy):
    apex_y = cy - R
    base_y = cy + R
    if py < apex_y or py > base_y:
        return False
    t = (py - apex_y) / (base_y - apex_y)
    span = (2.0 * R / math.sqrt(3.0)) * t
    return (cx - span) <= px <= (cx + span)


# ordem de pintura: de trás pra frente
LAYERS = (
    (_circle, BLUE, 78.0, 78.0),
    (_triangle, YELLOW, 150.0, 150.0),
    (_square, RED, 168.0, 78.0),
)


def _sample(px, py):
    out = (0, 0, 0, 0)
    for fn, col, cx, cy in LAYERS:
        if fn(px, py, cx, cy):
            out = col
    return out


def render(size):
    scale = DESIGN / size
    step = scale / SS
    base = step / 2
    inv = 1.0 / (SS * SS)
    buf = bytearray(size * size * 4)
    for j in range(size):
        for i in range(size):
            ar = ag = ab = aa = 0.0
            for sj in range(SS):
                py = (j * scale) + base + sj * step
                for si in range(SS):
                    px = (i * scale) + base + si * step
                    rr, gg, bb, a = _sample(px, py)
                    f = a / 255.0
                    ar += rr * f
                    ag += gg * f
                    ab += bb * f
                    aa += f
            o = (j * size + i) * 4
            if aa > 0:
                buf[o] = round(ar / aa)
                buf[o + 1] = round(ag / aa)
                buf[o + 2] = round(ab / aa)
            buf[o + 3] = round(aa * inv * 255)
    return bytes(buf)


def write_png(path, size, rgba):
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    raw = bytearray()
    row = size * 4
    for y in range(size):
        raw.append(0)
        raw += rgba[y * row:(y + 1) * row]
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    path.write_bytes(png)
    return png


def write_lvgl_bin(path, size, rgba):
    LV_IMAGE_HEADER_MAGIC = 0x19
    CF_ARGB8888 = 0x10
    stride = size * 4
    header = struct.pack("<BBHHHHH", LV_IMAGE_HEADER_MAGIC, CF_ARGB8888,
                         0, size, size, stride, 0)
    data = bytearray(size * size * 4)
    for p in range(size * size):
        r, g, b, a = rgba[p * 4:p * 4 + 4]
        data[p * 4] = b
        data[p * 4 + 1] = g
        data[p * 4 + 2] = r
        data[p * 4 + 3] = a
    blob = header + bytes(data)
    path.write_bytes(blob)
    return blob


def main(argv):
    check = "--check" in argv
    png_bytes = write_png(TOOL_DIR / ("icon.png.new" if check else "icon.png"),
                          240, render(240))
    bin_bytes = write_lvgl_bin(TOOL_DIR / ("icon.bin.new" if check else "icon.bin"),
                               64, render(64))
    if check:
        ok = True
        for name, new in (("icon.png", png_bytes), ("icon.bin", bin_bytes)):
            cur = (TOOL_DIR / name).read_bytes() if (TOOL_DIR / name).exists() else b""
            (TOOL_DIR / (name + ".new")).unlink()
            if cur != new:
                print(f"desatualizado: {name}", file=sys.stderr)
                ok = False
        return 0 if ok else 1
    print(f"icon.png  {len(png_bytes)} B  (240x240 RGBA)")
    print(f"icon.bin  {len(bin_bytes)} B  (64x64 LVGL v9 ARGB8888)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
