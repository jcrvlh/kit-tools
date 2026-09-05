#!/usr/bin/env python3
"""
make_icon.py — rasteriza a geometria do ícone (replicada abaixo) em:

  icon.png   240x240 RGBA   — catálogo web / previews
  icon.bin    64x64  LVGL v9 (ARGB8888) — asset do pacote .kit

Sem dependências: renderer por supersampling + encoder PNG em stdlib.

    python3 scripts/make_icon.py            # gera na pasta da Tool
    python3 scripts/make_icon.py --check    # falha se os arquivos estão desatualizados
"""
from __future__ import annotations

import math
import struct
import sys
import zlib
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent

# --- Geometria no espaço de projeto 240x240 (celular: corpo + alto-falante +
# botão, mesmo desenho do ícone geométrico do Launcher em kit_launcher.c) ----
DESIGN = 240.0
BODY = (60.0, 20.0, 120.0, 195.0, 26.0)         # x, y, w, h, r — corpo do aparelho
PARTS = (
    (100.0, 46.0, 40.0, 10.0, 5.0),             # alto-falante
    (106.0, 174.0, 28.0, 28.0, 14.0),           # botão (r = w/2 vira círculo)
)
FIG = (0xC6, 0x47, 0x2F, 255)                   # KIT_COLOR_RED

SS = 4  # subamostras por eixo


def _round_rect_sdf(px, py, x, y, w, h, r):
    cx, cy = x + w / 2, y + h / 2
    qx = abs(px - cx) - (w / 2 - r)
    qy = abs(py - cy) - (h / 2 - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def _sample(px, py):
    if _round_rect_sdf(px, py, *BODY) <= 0:
        return FIG
    for part in PARTS:
        if _round_rect_sdf(px, py, *part) <= 0:
            return FIG
    return (0, 0, 0, 0)


def render(size):
    scale = DESIGN / size
    buf = bytearray(size * size * 4)
    step = scale / SS
    base = step / 2
    inv = 1.0 / (SS * SS)
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
    """LVGL v9 image binary — LV_COLOR_FORMAT_ARGB8888 (cf 0x10), header 12 B."""
    LV_IMAGE_HEADER_MAGIC = 0x19
    CF_ARGB8888 = 0x10
    stride = size * 4
    header = struct.pack("<BBHHHHH", LV_IMAGE_HEADER_MAGIC, CF_ARGB8888,
                         0, size, size, stride, 0)
    data = bytearray(size * size * 4)
    for p in range(size * size):
        r, g, b, a = rgba[p * 4:p * 4 + 4]
        data[p * 4]     = b
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
