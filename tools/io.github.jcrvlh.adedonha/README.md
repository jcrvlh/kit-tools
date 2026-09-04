# Adedonha

Mini-jogo de mesa "nome, lugar, objeto" (também **Stop!** ou **Adedanha**) para
o KIT. Fiel à filosofia do Bingo — **o KIT sorteia, o papel confere**: o KIT
sorteia a **cartela** (as categorias da rodada) e as **letras**, conta o tempo e
toca o alarme. Ninguém pontua no aparelho — cada um confere e soma no papel.

## Fluxo

1. **Sorteia a cartela uma vez** → todos copiam as categorias como colunas (ou
   apontam a câmera no **QR** da página CARTELA e imprimem as folhas).
2. **Sorteia uma letra** (botão, toque no palco ou **chacoalhando**) → o tempo
   começa a correr.
3. Todos preenchem uma palavra por coluna com aquela letra, até **o tempo
   acabar** (alarme) ou alguém apertar **STOP**.
4. Conferem e pontuam no papel. **Sorteia a próxima letra** na mesma cartela.

## Tela

Titlebar + `lv_tileview` de 3 páginas (`AJUSTE ◄──► JOGO ◄──► CARTELA`, começa
no JOGO) + botão sensível ao estado fixo no rodapé.

### AJUSTE

| Campo | Opções |
|---|---|
| **CATEGORIAS** | `4` · `6` · `8` — quantas a cartela sorteia (trocar pede uma cartela nova) |
| **TEMPO** | `30S` · `1MIN` · `2MIN` · `OFF` — `OFF` mostra um cronômetro subindo e a rodada só acaba no STOP |
| **LETRAS** | `FÁCEIS` (16 letras que dá pra preencher a cartela inteira) · `TODAS` (A–Z) |

Mais `NOVA CARTELA` (um toque) e `REINICIAR LETRAS` (dois toques — devolve todas
ao sorteio). **Só os ajustes** persistem; a cartela e o saco de letras começam
do zero a cada abertura.

### JOGO

A letra sorteada em `kit_display_72`, o relógio `MM:SS` em `kit_display_44`, e o
botão. Um anel pulsa nos últimos 10 s. Estados: `SORTEAR CARTELA` → `SORTEAR
LETRA` (roleta A–Z, **sem reposição**) → `STOP` → próxima letra.

**Tempo esgotado** → overlay azul `TEMPO` com o alarme. O **1º toque cala o
alarme**; o 2º (ou o botão `PRÓXIMA LETRA`) sorteia a próxima letra.

### CARTELA

Antes do primeiro sorteio, o **"como joga"** no padrão da Mímica — um corpo em
`kit_sans_28` (caixa normal, quebra linha, rolável), não mais quatro linhas de
mono apagado.

Depois do sorteio: a **lista numerada das categorias** + um **QR** pro gerador de
folhas web (`web-installer/adedonha.html`, publicado em
`jcrvlh.github.io/kit/adedonha.html`). O QR leva os slugs das categorias da
rodada na URL (`?c=nome,animal,fruta,…`); a página monta uma folha A4 por pessoa
(coluna da LETRA + uma coluna por categoria + PONTOS, N rodadas em branco) e
imprime, ou — no alternador **CATEGORIAS / PREENCHER** — deixa anotar as
respostas no próprio celular (salvo em `localStorage`). A nota puxa pro papel e
caneta — a web fica como alternativa, e ela também imprime.

Exige **runtime ≥ 0.3.1** — `lv_qrcode_*` (QR, entrou em 0.3.0) e `strcpy` (que o
GCC sintetiza no `.so`; entrou em 0.3.1). O `kit-cli build --target xtensa`
compila com `-DLV_USE_QRCODE=1`.

## Sorteios

- **Letras sem reposição** (saco de 16 ou 26) via Random API / TRNG. Saco vazio
  → reembaralha sozinho. Trocar `FÁCEIS`↔`TODAS`, trocar as CATEGORIAS ou tocar
  `REINICIAR LETRAS` zera o saco.
- **Cartela**: N categorias distintas de um pool fixo de 43, Fisher-Yates.

## Identidade sonora

Quatro SFX próprios do Core (`KIT_SFX_ADEDONHA_CARD/LETTER/STOP/TIMEUP`): folhear
cartas na cartela, carimbo + "VALENDO!" na letra, buzina amigável no STOP, klaxon
de game-show no tempo esgotado. Os tiques dos últimos 5 s reusam
`KIT_SFX_TIMER_TICK`.

## Créditos

Baralho de categorias original, licença CC0. Autor: jcrvlh.
