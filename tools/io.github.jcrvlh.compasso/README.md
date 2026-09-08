# Compasso

**Compasso** é um treino de percepção de tempo. A cada rodada aparece um
alvo em segundos (sorteado entre 5 e 60), você memoriza, toca em **APAGAR**
e a tela fica preta. Toque de novo quando achar que o tempo passou — o
Compasso revela o seu tempo real e o erro em porcentagem. No fim de 3
rodadas vem o veredito: quanto o seu "relógio interno" corta ou estica o
tempo, sempre.

---

## Como Jogar

1. Escolha o **modo** no AJUSTE (SOLO ou DUPLA) e volte pro JOGO.
2. Memorize o alvo em segundos e toque em **APAGAR**.
3. Com a tela preta, toque quando achar que o tempo acabou.
4. Veja o erro da rodada. Depois de 3 rodadas, o veredito do seu relógio
   interno — **consistência importa mais que acerto**: quem erra sempre o
   mesmo tanto só precisa compensar.

### DUPLA (melhor de 3)

Mesmo alvo pros dois. Na tela preta cada um toca no **seu lado** — uma linha
verde vertical divide os campos e **não apaga**. O primeiro toque trava
aquele lado e espera o outro. Ponto pro mais perto; empate não pontua.

### MELHORES (só SOLO)

Top-5 por **menor erro médio** numa partida de 3 rodadas. Entrou? Arraste
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
