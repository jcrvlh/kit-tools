#!/usr/bin/env python3
"""
build_catalog.py — gera catalog/index.json a partir de tools/*/manifest.json.

Uso:
    python scripts/build_catalog.py [--dist DIR] [--out catalog/index.json]

--dist DIR   diretório com os pacotes <id>.kit já construídos; quando presente,
             o script preenche sha256 e tamanho de cada pacote.

O catálogo segue docs/tools/registry.md do repositório principal (jcrvlh/kit).
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import sys
from pathlib import Path

REPO = "jcrvlh/kit-tools"
RELEASE_URL = "https://github.com/{repo}/releases/download/{id}-v{version}/{id}.kit"
SOURCE_URL = "https://github.com/{repo}/tree/main/tools/{id}"
PAGES_ICON_URL = "https://jcrvlh.github.io/kit-tools/icons/{id}.png"

MIRRORED_FIELDS = (
    "name", "version", "version_code", "min_runtime", "max_runtime",
    "author", "description", "permissions", "api_level",
)


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def _tool_entry(manifest: dict, dist: Path | None) -> dict:
    tid = manifest["id"]
    entry = {"id": tid}
    for field in MIRRORED_FIELDS:
        if field in manifest:
            entry[field] = manifest[field]

    # Trilha de confiança: reservado => oficial; demais => comunidade.
    reserved = tid.startswith(("com.kit.", "org.kit."))
    entry["tier"] = "official" if reserved else "community"
    entry["size_installed"] = manifest.get("size_installed")

    package = {
        "url": RELEASE_URL.format(repo=REPO, id=tid, version=manifest["version"]),
        "sha256": None,
        "size": None,
    }
    if dist is not None:
        kit_path = dist / f"{tid}.kit"
        if kit_path.exists():
            package["sha256"] = _sha256(kit_path)
            package["size"] = kit_path.stat().st_size
    entry["package"] = package

    entry["icon_url"] = PAGES_ICON_URL.format(id=tid)
    entry["source_url"] = SOURCE_URL.format(repo=REPO, id=tid)
    entry["updated_at"] = _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return entry


def build(tools_dir: Path, dist: Path | None) -> dict:
    tools = []
    for manifest_path in sorted(tools_dir.glob("*/manifest.json")):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        tid = manifest.get("id", "")
        if manifest_path.parent.name != tid:
            raise SystemExit(
                f"{manifest_path}: id '{tid}' != nome da pasta '{manifest_path.parent.name}'"
            )
        tools.append(_tool_entry(manifest, dist))

    return {
        "catalog_version": 1,
        "generated_at": _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "keys": [],           # preenchido pela pipeline de assinatura (ADR-0012)
        "revoked": [],
        "tools": tools,
    }


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parent.parent
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dist", type=Path, default=None)
    p.add_argument("--out", type=Path, default=root / "catalog" / "index.json")
    args = p.parse_args(argv)

    catalog = build(root / "tools", args.dist)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{args.out}: {len(catalog['tools'])} Tool(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
