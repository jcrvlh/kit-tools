# Trinca — `io.github.jcrvlh.trinca`

Caça-níquel **honesto** e sem propósito: não tem aposta, ficha nem prêmio, só
você e a sorte. Toque em **GIRAR** (ou nos rolos) ou chacoalhe o KIT, e os três
rolos param da esquerda pra direita. Dois iguais na linha do meio é **dupla**,
três iguais é **trinca**, três estrelas é a **trinca rara**.

O que a diferencia de um slot qualquer é a transparência. As chances estão
escritas, o sorteio é limpo e uma barra mostra se a sessão está com mais ou
menos sorte que o acaso puro.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Girar chacoalhando:** sim ou não.
  - **Velocidade:** normal (~1,8 s até o último rolo) ou rápida (~1 s).
  - **Na vida deste KIT:** giros, duplas, trincas e raras, cada um ao lado do
    valor **esperado** pro mesmo número de giros, e o giro da 1ª trinca rara.
  - **Zerar histórico** (pede um segundo toque).
- **JOGO:** placar da sessão, os 3 rolos, a barra de sorte e **GIRAR**.
- **COMO JOGA:** regras, a tabela de chances e o que a barra diz (e não diz).

## Os rolos e as chances

Os 3 rolos usam a mesma **faixa de 32 paradas**: 6 símbolos comuns (círculo
azul, triângulo amarelo, quadrado vermelho, losango verde, cruz e anel) com 5
paradas cada, mais a **estrela** com 2. Nenhum símbolo encosta em si mesmo na
faixa. Um giro sorteia **uma parada uniforme por rolo**, independente dos
outros, pela Random API (TRNG de hardware).

| Resultado | Chance exata | |
| :--- | ---: | ---: |
| Nada | 19.500 / 32.768 | 59,5% |
| Dupla | 12.510 / 32.768 | 38,2% |
| Trinca comum | 750 / 32.768 | 2,3% |
| Trinca rara (3 estrelas) | 8 / 32.768 | 1 em 4.096 |

Trinca de qualquer tipo sai 1 vez em 43. A tabela não é digitada no código:
`trinca_odds()` a calcula a partir da faixa, e o teste confere a conta contra
as 32.768 combinações uma a uma e contra 2 milhões de giros sorteados.

### Honestidade

O resultado sai **no toque**. A animação só rola a faixa real até as paradas
sorteadas: o que passa na tela é a faixa de verdade, sem quase-acerto fabricado
e sem "escorregão da casa".

## A barra de sorte

Cada giro vale pontos: **nada 0, dupla 1, trinca 3** (a rara conta como
trinca, senão uma única estrela congelaria a barra no 99%). A barra mostra o
**percentil** dos pontos da sessão contra todos os resultados possíveis do
mesmo número de giros: "mais sorte que 91%" quer dizer que, no acaso puro, 91%
das sessões do mesmo tamanho fazem menos pontos (empate conta metade). Abaixo
de 50% vira "menos sorte que X%". Nunca mostra 0% nem 100%.

- **Exata, não aproximada.** A distribuição da soma é mantida por convolução
  incremental, um giro por vez. A aproximação normal erraria justamente nas
  pontas, onde a trinca mora.
- **Inteiro de 32 bits.** O `.so` não tem float nem aritmética de 64 bits. A
  distribuição fica em escala 2^30 e cada produto é partido em metades de 15
  bits. Contra a mesma conta em `double`, o erro máximo do percentil é 0,5
  milésimo.
- **Sessão, a partir de 20 giros.** Abaixo disso é ruído, e a legenda mostra
  `MEDINDO 7/20`.
- **Janela de 300 giros.** Num contador vitalício a lei dos grandes números
  empurra todo mundo pros 50%. Depois de 300 giros a barra mede só os últimos
  300. O vitalício fica no AJUSTE, como número seco.
- **Retrospectiva, nunca preditiva.** Cada giro é independente. O texto diz
  "até agora" e o COMO JOGA diz com todas as letras que azar acumulado não
  aproxima a próxima trinca. Nada de "tá devendo".

## Som

Tique do `audio->fuse` como catraca enquanto os rolos giram. A tensão cai a
cada rolo que para, então o tique desacelera junto. Tem um "clac" grave por
parada, duas notas subindo na dupla, `KIT_SFX_REVEAL` + rolos piscando na
trinca e `KIT_SFX_ONBOARD_DONE` + tela da estrela na trinca rara.

## Persistência

Ajustes (`tr_shake`, `tr_speed`) e números da vida (`tr_giros`, `tr_duplas`,
`tr_trincas`, `tr_raras`, `tr_rara1`), gravados em lote e ao sair. A sessão (e
com ela a barra) começa do zero a cada vez que a Trinca abre, de propósito.

## Build

```bash
cmake -B build -S . -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake --build build && ctest --test-dir build      # lógica (test_trinca)
kit-cli build . --target xtensa                     # tool.so
```

- A lógica pura está em `src/trinca_game.c` (inteiro puro).
- `src/symbols.h` (máscaras A8 64×64 dos 7 símbolos) sai de
  `scripts/make_symbols.py`.
- O ícone sai de `scripts/make_icon.py`, com `icon.svg` como fonte de verdade.

## Licença

GPL-3.0, como o resto do catálogo.
