#!/usr/bin/env python3
"""
make_icon.py — rasteriza icon.svg (geometria replicada abaixo) em:

  icon.png   240x240 RGBA   — catálogo web / previews
  icon.bin    64x64  LVGL v9 (ARGB8888) — asset do pacote .kit

Sem dependências: renderer por supersampling + encoder PNG em stdlib.

    python3 scripts/make_icon.py            # gera na pasta da Tool
    python3 scripts/make_icon.py --check    # falha se os arquivos estão desatualizados

Se o firmware passar a renderizar icon.bin com outro color format, regenerar.
"""
from __future__ import annotations

import math
import struct
import sys
import zlib
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent

# --- Geometria no espaço de projeto 240x240 (espelha icon.svg) --------------
DESIGN = 240.0
HEAD = (28.0, 74.0, 132.0, 132.0, 66.0)         # x, y, w, h, r  (círculo)
DEVICE = (112.0, 44.0, 70.0, 128.0, 14.0)       # x, y, w, h, r
DEVICE_STROKE = 6.0
BARS = (
    (150.0, 92.0,  58.0, 16.0, 8.0),
    (150.0, 140.0, 58.0, 16.0, 8.0),
)

FILL    = (0x0A, 0x0A, 0x0B, 255)
BORDER  = (0x3A, 0x3A, 0x3E, 255)
ACCENT  = (0xE9, 0xB2, 0x3C, 255)               # KIT_COLOR_YELLOW

SS = 4  # subamostras por eixo


def _round_rect_sdf(px, py, x, y, w, h, r):
    cx, cy = x + w / 2, y + h / 2
    qx = abs(px - cx) - (w / 2 - r)
    qy = abs(py - cy) - (h / 2 - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def _sample(px, py):
    """Cor opaca RGBA (a=0 fora do ícone) para um ponto no espaço 240."""
    # setas de inclinar (topo da pilha)
    for bar in BARS:
        if _round_rect_sdf(px, py, *bar) <= 0:
            return ACCENT
    # aparelho na testa: borda amarela + miolo escuro
    ds = _round_rect_sdf(px, py, *DEVICE)
    if ds <= 0:
        x, y, w, h, r = DEVICE
        inner = (x + DEVICE_STROKE, y + DEVICE_STROKE,
                 w - 2 * DEVICE_STROKE, h - 2 * DEVICE_STROKE, r - DEVICE_STROKE)
        return FILL if _round_rect_sdf(px, py, *inner) <= 0 else ACCENT
    # cabeça: borda fina + miolo escuro
    hs = _round_rect_sdf(px, py, *HEAD)
    if hs <= 0:
        x, y, w, h, r = HEAD
        inner = (x + 3.0, y + 3.0, w - 6.0, h - 6.0, r - 3.0)
        return FILL if _round_rect_sdf(px, py, *inner) <= 0 else BORDER
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
        data[p * 4]     = b          # lv_color32_t: blue, green, red, alpha
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
