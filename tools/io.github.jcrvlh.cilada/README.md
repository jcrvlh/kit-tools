# Cilada — `io.github.jcrvlh.cilada`

Mini-jogo de mesa inspirado no "pula pirata". Uma grade de furos com **uma
cilada** escondida. O KIT passa de mão em mão: na sua vez, você **segura** o
dedo num furo até o anel encher. Furo seguro, passa adiante. Cilada: uma
bola vermelha salta do furo e você **sai da roda**. Sobrou um, ganhou.

Diferente do [Estouro](../io.github.jcrvlh.estouro) e do
[Pavio](../io.github.jcrvlh.pavio), onde o perigo é invisível (limiar ou
tempo), aqui o risco está **à vista**: a grade encolhe, o "1 EM N" desce e a
mesa inteira vê onde cada um escolheu mexer.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Jogadores:** 2 a 12.
  - **Grade:** 3×3 (rápida) ou 4×4.
  - **Cartas:** com ou sem.
  - Durante a partida os ajustes travam e aparece **ENCERRAR PARTIDA** (pede
    um segundo toque).
- **JOGO**
  - Parado: número de jogadores, a grade e as cartas escolhidas + **COMEÇAR**.
  - Jogando: instrução da vez à esquerda (`SEGURE UM FURO`, `ABRA 2 FUROS`,
    `ESPIE UM FURO`, `PASSE O KIT`...) e a chance à direita (`1 EM N`, fica
    vermelha com 3 ou menos furos).
- **COMO JOGA:** as regras.

## Regras

1. Na sua vez, segure um furo até o anel encher (0,6 s). Soltar antes não
   abre nada — evita abrir sem querer ao passar o KIT.
2. Furo seguro vira um disco apagado. Passe o KIT.
3. Abriu a cilada: sai da roda e **escolhe quem começa a próxima rodada**
   (grade nova). Isso compensa a vantagem de quem começa.
4. **Tremor:** às vezes um furo treme. A cilada tem peso 2 no sorteio do
   tremor (no 3×3, cai nela ~20% das vezes contra ~11% do acaso) — pista de
   verdade, nunca certeza.
5. **Cartas** (2 no 3×3, 3 no 4×4, no máximo uma de cada por rodada,
   escondidas em furos seguros):
   - **ESPIA** — quem acha vira o KIT pra si e segura um furo: só ele vê se é
     seguro. Pode mentir pra mesa.
   - **APONTA** — quem acha escolhe quem joga depois dele.
   - **+1** — quem jogar depois abre 2 furos.
   - **ESCUDO** — quem acha guarda. Se cair na cilada nesta rodada, fica no
     jogo e a cilada se esconde em outro furo fechado (de preferência sem
     carta). O furo desarmado fica marcado em vermelho apagado.

O KIT não sabe quem está segurando o aparelho: ele conta aberturas que faltam
na vez, escudos guardados e jogadores na roda. Quem tem o escudo, quem aponta
e quem saiu, a mesa resolve. Ao cair na cilada com escudo na mesa, o KIT
pergunta: **TENHO ESCUDO** / **NÃO, SAÍ**.

## Gatilhos

| Gesto | Efeito |
|---|---|
| Segurar um furo fechado (0,6 s) | abre (ou espia, depois da ESPIA) |
| Soltar / arrastar antes de encher | cancela |
| Deslizar na horizontal | troca de página |

## Som

Batida de coração enquanto o anel enche — acelera quando sobram poucos furos
(a chance real por toque não muda; a mesa jura que muda). Duas notas subindo
no furo seguro, `KIT_SFX_REVEAL` na carta, uma "mola" subindo na cilada e
`KIT_SFX_ONBOARD_DONE` no fim. A espiada toca o mesmo clique nos dois
resultados — o som não entrega a resposta.

## Persistência

Ajustes e partida em andamento (chaves `cil_*`): fechar e reabrir continua de
onde parou, inclusive no meio de uma carta ou da pergunta do escudo. A lógica é
aplicada e salva antes de cada animação. Mantém a tela acesa
(`power->keep_awake`) só durante a partida.

## Build

```bash
cmake -B build -S . -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake --build build && ctest --test-dir build      # lógica (test_cilada)
kit-cli build . --target xtensa                     # tool.so
```

A lógica pura está em `src/cilada_game.c` (inteiro puro, sem float). O ícone
sai de `scripts/make_icon.py` (`icon.svg` é a fonte de verdade).

## Licença

GPL-3.0, como o resto do catálogo.
