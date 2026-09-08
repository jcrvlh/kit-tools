# Compasso

**Compasso** é um treino de percepção de tempo. A cada rodada aparece um
alvo em segundos, você toca em **APAGAR** e a tela escurece — a meta
continua à vista, não é sobre decorar. Toque de novo quando achar que o
tempo passou — o Compasso revela o seu tempo real e o erro em porcentagem.
No fim vem o veredito: quanto o seu "relógio interno" corta ou estica o
tempo, sempre.

O alvo **cresce ao longo da partida**: começa curto (~10 s) e sobe até
~55 s na última rodada. No AJUSTE você escolhe **1, 3 (padrão) ou 5**
rodadas.

---

## Como Jogar

1. No AJUSTE, escolha **modo** (SOLO / DUPLA) e **rodadas** (1 / 3 / 5).
2. Toque em **APAGAR**. A tela escurece.
3. Toque quando achar que o tempo acabou.
4. Veja o erro da rodada. No fim, o veredito do seu relógio interno —
   **consistência importa mais que acerto**: quem erra sempre o mesmo tanto
   só precisa compensar.

### DUPLA (melhor de 3)

Mesmo alvo pros dois. Na tela escura cada um toca no **seu lado** (**J1** à
esquerda, **J2** à direita) — uma linha verde vertical divide os campos e
**não apaga**. Ao tocar, aquele lado marca ✓ e espera o outro. Ponto pro
mais perto; empate não pontua.

### MELHORES (só SOLO)

Top-5 por **menor erro médio**, **um ranking separado por contagem de
rodadas** (um jogo de 1 rodada não compara com um de 5). Entrou? Arraste
pra cima ou pra baixo em cada caixa pra girar a letra da sigla — como uma
roleta — ou toque pra avançar uma. Toque em **SALVAR**.

---

## Notas de implementação

- Tool de catálogo (`.so`). Tempo lido do RTC em milissegundos; toda a
  matemática de erro é inteira de 32 bits (o carregador de Tools não resolve
  aritmética de float).
- UI com a galeria `kit_ui.h` (shell + tileview, chips, página COMO JOGA,
  botão de ação, seletor de sigla).
- `min_runtime` **0.3.1** (o `kit_ui.h` usa a síntese de `strcpy`/`strncpy`
  a partir de `snprintf`).
