# Mímica

Mini-jogo de mesa: "atue a palavra por gestos, sem falar".

Uma pessoa segura o KIT, vê a **palavra ou frase** grande no centro (com a
**categoria** acima) e a **representa por gestos** — sem falar, sem fazer som,
sem mexer a boca formando a palavra, sem apontar objeto da sala, sem soletrar no
ar. O time dela adivinha. Acertou, toca em **ACERTOU** e cai a próxima carta;
travou, toca em **PULAR**. Quando o **TEMPO** acaba, o KIT mostra quantas ela fez
— mas quem valida cada palpite é a roda (filosofia do Bingo).

## Telas

Titlebar fixa + `lv_tileview` de 3 páginas (começa no JOGO):

- **AJUSTE** — `TEMPO` (`60S / 90S / 120S / OFF`), `CATEGORIA` (`MOSTRA` /
  `ESCONDE`), `BARALHO` (`FÁCIL` / `TUDO`), `PULOS` (`LIVRES` / `3` / `0`) e
  `REINICIAR BARALHO`. Só os ajustes vão pro Storage.
- **JOGO** — a categoria (kicker), a palavra em `kit_display_44` (ou `kit_sans_28`
  se longa), a barra de tempo e os botões `PULAR` (contornado) + `ACERTOU`
  (cheio). Preparo de 3 s (`PREPARE-SE 3 · 2 · 1`) antes do relógio arrancar.
- **COMO JOGA** — as regras, corpo rolável.

Tempo esgotado → overlay azul `TEMPO` (ou `FIM` no modo `OFF`) com o placar da
vez (`ACERTOS n` / `PULOS n`) e o botão `PASSAR A VEZ`. Um toque em qualquer
lugar do overlay cala o alarme.

## Baralho

Fixo na Tool (`src/main.c`, ~124 cartas `{ texto, categoria }`). Categorias
`FÁCIL`: `AÇÃO` `OBJETO` `ANIMAL` `PROFISSÃO` `LUGAR` `ESPORTE`; `TUDO`
acrescenta `PERSONAGEM` `EXPRESSÃO` `EMOÇÃO`. Saco sem reposição (Fisher-Yates);
`PULAR` devolve a carta ao monte numa posição aleatória à frente.

Sem PWR (só built-ins têm `primary_action`) e sem chacoalhar — a mecânica é
toque. Permissões: `display`, `input`, `random`, `storage`, `audio`.

## Como testar

```bash
export KIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake -B build -S . -DKIT_SDK_PATH="$KIT_SDK_PATH" && cmake --build build
kit-cli build --target xtensa
kit-cli pack . -o io.github.jcrvlh.mimica.kit
```

No KIT: instale o `.kit` pelo Catálogo (ou copie pra `/sdcard/tools/`) e abra
**Mímica** na Home.

## Licença

Código da Tool sob GPL-3.0 (mesma da infraestrutura).
