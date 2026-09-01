# catalog/

`index.json` é **gerado** por `scripts/build_catalog.py` a partir de
`tools/*/manifest.json` — não é versionado (ver `.gitignore`).

A CI (`pages.yml`) o regenera a cada push na `main` e o publica, junto de
`index.html` e `schema/`, em **https://jcrvlh.github.io/kit-tools/**.

Para inspecionar localmente:

```bash
python scripts/build_catalog.py
cat catalog/index.json
```
