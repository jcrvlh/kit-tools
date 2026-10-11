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
  - **Velocidade:** normal (1º rolo em 0,75–1,05 s, o último sempre em 1,9 s,
    junto com o "parou" da catraca) ou rápida (todos sorteados, ~1 s, sem
    catraca).
  - **Histórico:** tabela com giros, duplas, trincas e raras de todos os giros
    deste KIT. A coluna **VOCÊ** mostra o que saiu e a coluna **ACASO**, a
    média que o acaso puro daria no mesmo número de giros. Embaixo vai o giro
    da 1ª trinca rara.
  - **Zerar histórico** (pede um segundo toque).
- **JOGO:** placar da sessão, os 3 rolos, a barra de sorte e **GIRAR**.
- **COMO JOGA:** curto: regras, chances, a barra e o que ela não diz.

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

## Animação

Cada rolo tem duração e número de voltas **sorteados a cada giro**, então a
velocidade muda de rolo pra rolo e de giro pra giro. A ordem de parada
(esquerda → direita) é sempre a mesma. Esse sorteio acontece depois do
resultado e não olha pra ele, então a velocidade não entrega nada.

Fora da linha do meio o símbolo **esmaece por cor**, misturado com o fundo do
rolo, em vez de ter uma sombra por cima. Assim o contorno amarelo da
dupla/trinca, que é um objeto à parte desenhado por cima, fica inteiro.

## Som

O firmware toca bipes de **até 14 ms a ~1/3 da amplitude** e todo o resto
(SFX prontos, bipes longos, o tique do `fuse`) bem mais alto.

- **Catraca (normal):** `KIT_SFX_BOTTLE_SPIN`, a catraca de madeira da
  Garrafa, que desacelera em ~1,9 s e fecha com um "parou". Ela roda inteira
  na task de áudio, com silêncio ativo entre os tiques. A versão anterior
  (bipes soltos disparados pelo timer, um por símbolo) estalava: entre um
  bipe e outro o DMA de áudio esvazia. Por isso o **último rolo para fixo em
  1,9 s**, casado com o "parou". Os dois primeiros seguem sorteados.
- **Sincronia:** a animação anda pelo relógio (`time->get_millis`), não pela
  contagem de quadros. Se a tela não segurar 50 fps, o rolo pula quadros em
  vez de atrasar em relação ao som.
- **Rápida:** a catraca não cabe em ~1 s. O giro é em silêncio, com um
  "clac" suave (bipe de 12 ms) por rolo.
- **Dupla:** duas notas suaves.
- **Trinca:** cascata pentatônica de dó subindo duas oitavas, trinado mi-sol e
  dó agudo, uma nota por passo de 70 ms com o contorno piscando junto. Cada
  nota são 2 bipes suaves colados. A fila de bipes do firmware tem 6 lugares
  e não espera, então a sequência é distribuída por um timer.
- **Rara:** a mesma cascata, por trás da tela da estrela.

O volume do KIT não é alterado: `set_volume` é global e não existe como ler o
valor original pra devolver na saída.

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
