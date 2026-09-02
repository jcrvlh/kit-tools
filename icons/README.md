# Ícones do catálogo

Um PNG por Tool, nomeado pelo `id`: `icons/<id>.png` (quadrado, fundo
transparente, ~240×240). É o `icon_url` que `scripts/build_catalog.py` grava no
`index.json` e que a pipeline de Pages publica em
`https://jcrvlh.github.io/kit-tools/icons/<id>.png`.

É separado do `icon.bin` que vai dentro do pacote `.kit` (formato de imagem do
LVGL, usado pelo dispositivo). Quando a Tool tem um `icon.svg` como fonte, gere
os dois a partir dele — ex.: `tools/io.github.jcrvlh.tarot/scripts/make_icon.py`.
