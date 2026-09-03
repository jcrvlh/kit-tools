# Pavio

**Pavio** é um mini-jogo de mesa para o KIT: o aparelho acende um pavio com
tempo **escondido** e mostra uma **sílaba grande no centro**. Cada jogador fala em
voz alta uma palavra que contenha aquela sílaba e **passa o aparelho adiante** —
tocando na tela ou chacoalhando o KIT —, antes que exploda. Quando o pavio
acaba, **BUM**: quem estiver com o KIT na mão perdeu a rodada!

Fiel à filosofia do Bingo e da Adedonha: o KIT dá o palco e o suspense, a mesa
fiscaliza as palavras.

---

## Como Jogar

1. **Ajuste** (opcional):
   - **PAVIO**: `CURTO` (~12–28 s), `MÉDIO` (~22–48 s) ou `LONGO` (~40–75 s). O tempo real é sorteado secretamente a cada rodada.
   - **SÍLABA**: `TROCA` a cada passe ou `FIXA` durante a rodada inteira.
   - **BARALHO**: `FÁCIL` (sílabas simples) ou `TUDO` (inclui encontros consonantais e dígrafos).
2. Toque em **ACENDER PAVIO**. O tique do pavio começa a queimar.
3. Fale uma palavra válida com a sílaba e **passe o KIT**: dê um toque na tela ou chacoalhe o aparelho.
4. O pavio vai acelerando continuamente. Quem estiver com o KIT quando fizer **BUM** perde a rodada!

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` e `kit_fonts.h`).
  - Página 0: **AJUSTE** (pílulas de seleção persistentes).
  - Página 1: **JOGO** (palco com sílaba em `kit_display_72`, pulso visual e botão).
  - Página 2: **COMO JOGA** (regras rápidas).
  - Overlay: **BUM** em vermelho com alarme de explosão e botão de nova rodada.
- **Áudio**: tique metronômico gerado na task de áudio do Runtime (`fuse`), acelerando de forma contínua e imune ao jitter de renderização da tela, e SFX de explosão `KIT_SFX_PAVIO_BOOM`.
- **Entradas**: toque na tela, botão de ação ou gesto de chacoalhar (IMU).
