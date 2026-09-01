# Tarot

Tiragens de tarot com interpretação em **texto** para o KIT. Não é uma simulação
de baralho físico: não há arte de carta nem "virar carta" com o dedo — a Tool
sorteia, mostra **nome + significado**, e sai da frente.

- **Baralho completo:** 78 cartas (22 Arcanos Maiores + 56 Menores).
- **Uma carta** — uma pergunta, uma resposta.
- **Três cartas** — Passado / Presente / Futuro, revelados um a um.
- **Cartas invertidas** — liga/desliga nos Ajustes; quando ligadas, cada carta
  pode sair normal ou invertida, e o texto acompanha a orientação.
- **Chacoalhe ou toque** para tirar.

Cada carta traz uma descrição do que ela é (para quem não conhece tarot), três
palavras-chave, o significado (normal/invertido) e, na tiragem de três, uma
frase de enquadramento para a posição.

O design completo está em [DESIGN.md](DESIGN.md).

## Como testar (build nativo)

```bash
# o KIT Tools SDK vem do repositório principal (jcrvlh/kit)
cmake -B build -S . -DKIT_SDK_PATH=/caminho/para/kit/tools-sdk
cmake --build build

./build/test_tarot   # testa o sorteio (sem reposição, chance de inversão) + sanidade do baralho
./build/tool.elf     # roda a Tool em modo stub (sem UI)
ctest --test-dir build
```

## Empacotar (Xtensa)

```bash
# toolchain do ESP-IDF no PATH + headers do LVGL 9.5.x:
#   export KIT_LVGL_DIR=/caminho/para/lvgl        (ou um build do firmware já feito)
kit-cli build . --target xtensa                   # gera tool.so
kit-cli pack  . -o io.github.jcrvlh.tarot.kit     # tool.so + manifest + icon.bin
```

Na prática o `.kit` de release sai da pipeline `publish.yml` do catálogo
(compila para o ESP32-S3, empacota e publica em Releases; assinatura Ed25519
pendente do ADR-0012). Ver
[tool_lvgl_runtime.md](https://github.com/jcrvlh/kit/blob/main/tools-sdk/docs/tool_lvgl_runtime.md).

## Ícone

`icon.svg` é a fonte de verdade (carta preta com o totem das três primitivas
Bauhaus). `scripts/make_icon.py` (só stdlib) rasteriza a partir da mesma
geometria:

```bash
python3 scripts/make_icon.py           # gera icon.png (240, catálogo) e icon.bin (64, LVGL v9)
python3 scripts/make_icon.py --check    # CI: falha se estiverem desatualizados
```

A cópia para o catálogo web fica em `../../icons/io.github.jcrvlh.tarot.png`.
Na Home do KIT, o card usa o glifo geométrico `home_icon: "card"` tingido pelo
`accent` — o `icon.bin` fica no pacote para quando o firmware renderizá-lo.

## Estado

- [x] Baralho completo: 78 cartas (`src/tarot_deck.c`) — 22 Maiores + Paus,
      Copas, Espadas e Ouros. Nenhum texto acima do limite da tela.
- [x] Sorteio + testes (`src/tarot_draw.c`, `test_tarot.c`)
- [x] UI: menu, concentração (shake/toque), embaralho, resultado 1 e 3 cartas, ajustes
- [x] Ícone (`icon.svg` → `icon.png` / `icon.bin`)
- [ ] Testar no hardware

## Licença

- **Conteúdo das cartas** (`src/tarot_deck.c`): texto 100% original, escrito para
  o KIT — **CC0 1.0** (domínio público).
- **Código**: GPL-3.0, igual ao catálogo.

Autor: [@jcrvlh](https://github.com/jcrvlh)
