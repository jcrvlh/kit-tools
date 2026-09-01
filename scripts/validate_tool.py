#!/usr/bin/env python3
"""
validate_tool.py — valida um ou mais diretórios de Tool antes do merge.

Uso:
    python scripts/validate_tool.py tools/<id> [tools/<outro-id> ...]
    python scripts/validate_tool.py --all

Verifica:
  - arquivos obrigatórios (manifest.json, CMakeLists.txt, src/, icon.bin, README.md);
  - conformidade do manifest.json (via kit_cli.validator quando disponível);
  - id == nome da pasta e no formato domínio reverso;
  - namespaces com.kit.* / org.kit.* são reservados (bloqueados sem --allow-reserved);
  - version_code inteiro > 0;
  - se catalog/index.json já traz o id, version_code deve ser maior que o publicado.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ID_RE = re.compile(r"^[a-z0-9_]+(\.[a-z0-9_]+)+$")
RESERVED = ("com.kit.", "org.kit.")
REQUIRED_FILES = ("manifest.json", "CMakeLists.txt", "icon.bin", "README.md")

try:
    from kit_cli.validator import validate_manifest_file  # type: ignore
except Exception:  # pragma: no cover - fallback quando o kit-cli não está instalado
    validate_manifest_file = None


def _published_version_codes() -> dict[str, int]:
    index = ROOT / "catalog" / "index.json"
    if not index.exists():
        return {}
    data = json.loads(index.read_text(encoding="utf-8"))
    return {t["id"]: t.get("version_code", 0) for t in data.get("tools", [])}


def validate_dir(tool_dir: Path, allow_reserved: bool, published: dict[str, int]) -> list[str]:
    errors: list[str] = []
    rel = tool_dir.relative_to(ROOT)

    for name in REQUIRED_FILES:
        if not (tool_dir / name).exists():
            errors.append(f"{rel}: arquivo obrigatório ausente: {name}")
    if not (tool_dir / "src").is_dir() or not any((tool_dir / "src").glob("*.c")):
        errors.append(f"{rel}: src/ deve conter ao menos um .c")

    manifest_path = tool_dir / "manifest.json"
    if not manifest_path.exists():
        return errors  # sem manifesto não dá pra seguir

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        errors.append(f"{rel}/manifest.json: JSON inválido: {e}")
        return errors

    if validate_manifest_file is not None:
        ok, verrs = validate_manifest_file(manifest_path)
        errors.extend(f"{rel}/manifest.json: {e}" for e in verrs if not ok)

    tid = manifest.get("id", "")
    if tid != tool_dir.name:
        errors.append(f"{rel}: id '{tid}' != nome da pasta '{tool_dir.name}'")
    if not ID_RE.match(tid):
        errors.append(f"{rel}: id '{tid}' fora do formato domínio reverso")
    if tid.startswith(RESERVED) and not allow_reserved:
        errors.append(f"{rel}: namespace reservado ({tid}); use seu próprio domínio reverso")

    vc = manifest.get("version_code")
    if not isinstance(vc, int) or vc <= 0:
        errors.append(f"{rel}: version_code deve ser inteiro > 0")
    elif tid in published and vc <= published[tid]:
        errors.append(
            f"{rel}: version_code {vc} <= publicado ({published[tid]}); incremente a versão"
        )

    return errors


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("dirs", nargs="*", type=Path)
    p.add_argument("--all", action="store_true", help="valida todas as Tools em tools/")
    p.add_argument("--allow-reserved", action="store_true", help="permite com.kit.* / org.kit.*")
    args = p.parse_args(argv)

    if args.all:
        targets = sorted(d for d in (ROOT / "tools").iterdir() if (d / "manifest.json").exists())
    else:
        targets = [d if d.is_absolute() else (ROOT / d) for d in args.dirs]
    if not targets:
        p.error("informe ao menos um diretório de Tool ou use --all")

    published = _published_version_codes()
    all_errors: list[str] = []
    for tool_dir in targets:
        errs = validate_dir(tool_dir, args.allow_reserved, published)
        status = "OK" if not errs else "FALHOU"
        print(f"[{status}] {tool_dir.relative_to(ROOT)}")
        all_errors.extend(errs)

    for e in all_errors:
        print(f"  - {e}", file=sys.stderr)
    return 1 if all_errors else 0


if __name__ == "__main__":
    sys.exit(main())
