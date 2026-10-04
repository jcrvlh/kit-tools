# Dobra — `io.github.jcrvlh.dobra`

Mini-jogo solo de juntar peças. Arraste o dedo no tabuleiro 4×4: todas as peças
escorrem para o lado do arraste e duas peças iguais que se encostam viram uma
só, com o **dobro** do valor. A cada jogada nasce uma peça nova (2 ou 4).
Chegue à meta antes de o tabuleiro encher.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Meta:** 256, 512, 1024 ou Infinito (sem meta, vale o recorde).
  - **Desfazer (chacoalhar):** desligado, 1×, 2× ou ilimitado por partida.
  - **Nova partida:** pede um segundo toque se a partida já tem pontos.
- **JOGO:** pontos, recorde da meta escolhida, desfazer restantes e o
  tabuleiro. A maior peça fica preenchida de verde.
- **COMO JOGA:** as regras.

## Gatilhos

| Gesto | Efeito |
|---|---|
| Arrastar em qualquer ponto da página JOGO | jogada (dispara assim que o dedo anda ~36 px) |
| Deslizar na faixa do título | troca de página (é o único jeito — o resto da tela não troca) |
| Chacoalhar | desfaz a última jogada (se o AJUSTE permitir) |

## Persistência

A partida em andamento, os pontos, os desfazer restantes, os ajustes e um
recorde por meta ficam salvos (chaves `dobra_*`). Fechar e reabrir continua de
onde parou.

## Build

```bash
cmake -B build -S . -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake --build build && ctest --test-dir build      # lógica (test_dobra)
kit-cli build . --target xtensa                     # tool.so
```

A lógica pura está em `src/dobra_game.c` (inteiro puro, sem float). O ícone sai
de `scripts/make_icon.py` (`icon.svg` é a fonte de verdade).
