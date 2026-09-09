# Repete

**Repete** é um jogo de memória de mesa para o KIT, no estilo "Genius" / Simon.
O KIT acende as **3 formas do KIT** numa sequência — círculo (azul), triângulo
(amarelo), quadrado (vermelho) — e você repete tocando nelas. Acertou a
sequência inteira? Ela ganha mais uma forma. Errou uma, acabou.

Cada forma tem uma cor e um som fixos (grave / médio / agudo), tanto no playback
quanto quando você toca.

---

## Como Jogar

1. Toque em **COMEÇAR**. O KIT acende as formas numa ordem — preste atenção.
2. Sua vez: repita a ordem tocando nas formas.
3. Acertou a sequência inteira? Ela ganha mais uma forma. Errou uma, acabou —
   o KIT mostra a forma certa e treme.
4. A cada 5 rodadas o KIT pisca as 3 formas juntas. O recorde de cada modo fica
   guardado.

No **AJUSTE** você troca o modo:

- **Clássico** — sequência +1 por rodada, velocidade fixa.
- **Velocidade** — igual, mas o playback acelera a cada rodada (com um piso).
- **Inverso** — repetir a sequência de trás pra frente.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` e `kit_fonts.h`).
  - Página 0: **AJUSTE** (seletor de modo: 2 chips + 1).
  - Página 1: **JOGO** (linha de status, as 3 teclas de forma, placar/recorde,
    botão COMEÇAR / JOGAR DE NOVO).
  - Página 2: **COMO JOGA** (regras).
- **Formas**: glifos `KIT_ICON_CIRCLE/TRIANGLE/SQUARE` em `kit_display_44`,
  dentro de teclas de 104 px. Acesa = fundo na cor da forma + borda + leve
  elevação; apagada = fundo `SURFACE` com o glifo na cor.
- **Áudio**: `audio->beep` por forma (330 / 523 / 784 Hz), `KIT_SFX_CONFIRM` no
  marco de 5, `KIT_SFX_ESTOURO_POP` no erro, `KIT_SFX_TOOL_OPEN` ao começar.
- **Feedback do erro**: a forma certa fica acesa e a página treme (timer
  deslocando o tileview no eixo X).
- **Persistência**: `storage->set_i32` — modo escolhido (`repete_mode`) e
  recorde por modo (`repete_hi0/1/2`). A partida em andamento não sobrevive a
  fechar a Tool (a sequência é sorteada).
- **Entradas**: toque na tela (as 3 formas, o botão, os chips de modo).
