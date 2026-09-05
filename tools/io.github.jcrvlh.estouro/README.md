# Estouro

**Estouro** é um mini-jogo de mesa para o KIT: passa-o-KIT-e-reza. Cada
jogador escolhe **livremente** quanto chacoalha o aparelho antes de repassá-lo
adiante. Um limiar de chacoalhadas **escondido** — sorteado numa faixa, nunca
mostrado — decide quando estoura. Quem estiver segurando o KIT na hora **sai
da roda**. O limiar encolhe a cada eliminação: o fim de jogo fica mais tenso,
com estouros mais rápidos.

Diferente do [Pavio](../io.github.jcrvlh.pavio): ali o risco vem do
**tempo** (um pavio correndo sozinho); aqui o risco vem da **ação do
jogador** — cada chacoalhada aumenta o perigo.

---

## Como Jogar

1. **Ajuste**: escolha quantos jogadores vão entrar (2–16) e toque em
   **COMEÇAR**.
2. Chacoalhe o KIT quantas vezes quiser (pelo menos uma) e passe pro
   próximo. Ninguém sabe quantas faltam pra estourar — só dá pra sentir o
   aviso ficando mais vermelho e a tela mais agitada.
3. Estourou: quem estava segurando o KIT sai da roda. Toque na tela pra
   confirmar e a próxima rodada começa.
4. A cada jogador eliminado o balão fica mais curto — o fim do jogo fica
   mais tenso. Só sobra um: esse ganhou.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` e `kit_fonts.h`).
  - Página 0: **AJUSTE** (só o número de jogadores).
  - Página 1: **JOGO** (parado: número + botão COMEÇAR; jogando: aviso
    grande, sem número de jogadores na tela).
  - Página 2: **COMO JOGA** (regras).
  - Overlay: **ESTOUROU** (vermelho, piscando) ou **ACABOU** (cor da Tool,
    fixo, com um check em vez do "!").
- **Áudio**: tensão contínua no motor `audio->fuse` (reaproveitado do Pavio)
  + `KIT_SFX_ESTOURO_SHAKE` (thump forte a cada chacoalhada) +
  `KIT_SFX_ESTOURO_POP` (estouro, mais curto e sem cascata que o `BUM` do
  Pavio, pra não confundir os dois jogos).
- **Feedback visual**: pulso ambiente no fundo da página JOGO que acelera
  conforme a tensão sobe (curva calmo → alerta → perigo, igual à do áudio),
  mais um flash forte no instante de cada chacoalhada. As proporções em que
  o aviso muda de cor são sorteadas por rodada — não é um padrão fixo
  contável.
- **Entradas**: toque na tela (botão COMEÇAR, overlay) ou gesto de
  chacoalhar (IMU). Sem botão de ação na tela durante o jogo — só a
  chacoalhada de verdade.
