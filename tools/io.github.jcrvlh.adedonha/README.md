# Adedonha

Mini-jogo de mesa "nome, lugar, objeto" (também **Stop!** ou **Adedanha**) para
o KIT. Fiel à filosofia do Bingo — **o KIT sorteia, o papel confere**: o KIT
sorteia a **cartela** (as categorias da rodada) e as **letras**, conta o tempo e
toca o alarme. Ninguém pontua no aparelho — cada um confere e soma no papel.

## Fluxo

1. **Sorteia a cartela uma vez** → todos copiam as categorias como colunas.
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

A lista numerada das categorias sorteadas (o que se copia pro papel). Antes do
primeiro sorteio, mostra os 4 passos do "como joga".

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
