#!/usr/bin/env python3
"""
make_bank.py — gera src/duo_bank.h a partir de data/perguntas.tsv.

    python3 scripts/make_bank.py            # gera
    python3 scripts/make_bank.py --check    # falha se o .h está desatualizado

Regras checadas (o build quebra antes da placa):
  - 2 ou 4 opções; nas de 4, cada opção com até 10 caracteres (grade 2x2);
  - sem travessão, aspas curvas nem reticências de 1 caractere (a fonte do KIT
    não tem esses glifos);
  - pergunta repetida dentro do mesmo tema;
  - no máximo DUO_MAX_THEME_Q (96) perguntas por tema (bitmap de inéditas).

4ª coluna opcional "conhece": a pergunta só entra no modo ME CONHECE (fato que
os dois sabem, como "Como vocês se conheceram?": no COMBINA sempre bateria).

As opções saem em CAIXA ALTA (vão em mono/display). Texto fora do ASCII vira
escape octal, que nunca "come" o caractere seguinte como o \\x faria.
"""
from __future__ import annotations

import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
SRC = TOOL_DIR / "data" / "perguntas.tsv"
OUT = TOOL_DIR / "src" / "duo_bank.h"

THEMES = ["geral", "amigos", "casal", "polemica"]
MAX_PER_THEME = 96
MAX_OPT4 = 10
FORBIDDEN = "—–“”‘’…"
# kit_display_72: A-Z Ã Ç Õ 0-9 - espaço
D72 = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ÃÇÕ")


def c_str(s: str) -> str:
    out = []
    for ch in s:
        if ch in '"\\':
            out.append("\\" + ch)
        elif ord(ch) < 128:
            out.append(ch)
        else:
            out.extend(f"\\{b:03o}" for b in ch.encode("utf-8"))
    return '"' + "".join(out) + '"'


def fits72(o: str) -> bool:
    return len(o) <= 5 and all(c in D72 for c in o)


def load():
    bank = {t: [] for t in THEMES}
    errors = []
    for ln, line in enumerate(SRC.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) not in (3, 4) or (len(parts) == 4 and parts[3] != "conhece"):
            errors.append(f"linha {ln}: esperava tema, pergunta, opções [, conhece] separados por TAB")
            continue
        theme, q, opts = parts[:3]
        only_conhece = len(parts) == 4
        opts = opts.split("|")
        where = f"linha {ln} ({q})"
        if theme not in bank:
            errors.append(f"{where}: tema desconhecido '{theme}'")
            continue
        if len(opts) not in (2, 4):
            errors.append(f"{where}: {len(opts)} opções (só 2 ou 4)")
        if len(opts) == 4 and any(len(o) > MAX_OPT4 for o in opts):
            errors.append(f"{where}: opção com mais de {MAX_OPT4} caracteres")
        if any(c in FORBIDDEN for c in q + "".join(opts)):
            errors.append(f"{where}: caractere sem glifo na fonte do KIT")
        if any(q == x[0] for x in bank[theme]):
            errors.append(f"{where}: pergunta repetida no tema {theme}")
        bank[theme].append((q, [o.upper() for o in opts], only_conhece))
    for t, qs in bank.items():
        if len(qs) > MAX_PER_THEME:
            errors.append(f"tema {t}: {len(qs)} perguntas (máximo {MAX_PER_THEME})")
        if not qs:
            errors.append(f"tema {t}: vazio")
    return bank, errors


def render(bank) -> str:
    lines = [
        "/* GERADO por scripts/make_bank.py a partir de data/perguntas.tsv — não editar à mão. */",
        "#pragma once",
        '#include "duo_game.h"',
        "",
    ]
    for t in THEMES:
        lines.append(f"static const duo_q_t DUO_BANK_{t.upper()}[] = {{")
        for q, opts, only_conhece in bank[t]:
            mask = sum(1 << i for i, o in enumerate(opts) if fits72(o))
            o4 = opts + [""] * (4 - len(opts))
            flags = "DUO_F_ONLY_CONHECE" if only_conhece else "0"
            lines.append(
                f"    {{ {c_str(q)}, {{ {', '.join(c_str(o) for o in o4)} }}, {len(opts)}, {mask}, {flags} }},"
            )
        lines.append("};")
        lines.append("")
    lines.append("static const duo_theme_t DUO_THEMES[DUO_THEME_COUNT] = {")
    for t in THEMES:
        lines.append(f"    {{ DUO_BANK_{t.upper()}, (int)(sizeof DUO_BANK_{t.upper()} / sizeof DUO_BANK_{t.upper()}[0]) }},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    bank, errors = load()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    text = render(bank)
    if "--check" in sys.argv:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print("src/duo_bank.h desatualizado: rode scripts/make_bank.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(" ".join(f"{t}={len(bank[t])}" for t in THEMES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
