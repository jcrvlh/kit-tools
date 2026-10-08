# Divisa — `io.github.jcrvlh.divisa`

Duelo de toques pra **2 pessoas**, com o KIT deitado na mesa entre as duas. A
metade de baixo é do **vermelho**, a de cima é do **azul**. Quando a sua metade
acende, você toca o mais rápido que puder. Ninguém vê o placar durante o jogo:
ele só aparece na **apuração**, rodada a rodada, com a divisa andando pra quem
tocou mais. Ganha quem terminar com mais território.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Rodadas:** 3 ou 5.
  - **Ritmo das janelas:** normal (0,6–1,5 s) ou frenético (0,4–0,9 s).
- **JOGO:** a tela dividida (protagonista), os ajustes escolhidos e **COMEÇAR**.
  Começar abre a **arena** em tela cheia.
- **COMO JOGA:** as regras.

## A partida

1. **Entrada:** 3 bipes e um "vai".
2. **Rodada:** sequência de **pares de janelas**. Em cada par, um lado acende e
   toca; depois o outro, **pelo mesmo tempo** (sorteado por par). Quem abre o
   par alterna a cada par e entre rodadas (com uma moeda no começo da
   partida). O número de pares (4–7) também é sorteado: ninguém sabe quando a
   rodada acaba, e os dois somam exatamente o mesmo tempo de toque.
3. **Folga** de 300 ms entre janelas, sem contar nada — o dedo de quem acabou
   de tocar ainda está na tela quando a vez troca.
4. **Entre rodadas:** bolinhas na divisa marcam o progresso (feitas, atual,
   futuras) — sem texto, legível dos dois lados.
5. **Apuração:** por rodada, os toques dos dois são contados em ticks
   intercalados. A divisa fica parada no meio enquanto os dois ainda têm toque
   pra contar e só pende quando o menor acaba; o ritmo acelera no empate e
   desacelera na virada, com o tambor do `audio->fuse`. Entre rodadas a divisa
   pode virar de verdade — o vai-e-vem vem dos dados, não de animação
   inventada.
6. **Resultado:** os percentuais aparecem em cada território (o do azul de
   cabeça pra baixo, virado pra ele), o vencedor conquista a tela inteira e
   ficam **DE NOVO** / **SAIR**. Empate: **DESEMPATE** joga 1 rodada a mais.

Pra sair no meio: segurar o **X** na divisa (só aparece fora das janelas, pra
ninguém usar como trava).

## Single-touch e o dedo plantado

O CST820 lê **um** ponto. Com dois dedos na tela ele não "solta": só pula a
coordenada pro dedo novo. Sem cuidado, quem deixa o dedo na própria metade na
vez do outro trava os toques do rival de graça. Por isso:

- **Encostar na própria metade fora da vez desconta**: 1 na hora + 1 a cada
  125 ms encostado (a metade fica a 40% e toca um "bzz").
- As metades **não têm press-lock**: o pulo de coordenada vira `PRESS_LOST` na
  metade antiga + `PRESSING` na nova (o LVGL 9.5 não manda `PRESSED` pra quem
  "chega arrastado"), e o primeiro `PRESSING` conta como chegada.
- **Deslize não é pulo:** a leitura crua do touch (`input->register_callback`)
  separa um 2º dedo (salto ≥ 90 px entre leituras) de um dedo escorregando pela
  divisa — que não dá toque pra quem esfrega nem desconto injusto pro outro.

## Números de cabeça pra baixo

A rotação de objeto não está na tabela de símbolos das Tools. O percentual do
azul usa os glifos do `kit_display_72` (dígitos) e do `kit_display_44` (`%`)
**girados 180°**, embutidos como imagens A8 em `src/digits_rot.h` — gerado por
`scripts/make_digits.py` a partir dos `.c` das fontes do firmware, então é o
mesmo desenho do número em pé.

```bash
python3 scripts/make_digits.py ~/Projetos/kit/firmware/components/kit_fonts/src
```

## Som

Tom de abertura por lado (grave pro vermelho, agudo pro azul), clique por
toque, "bzz" no desconto, apito de fim de rodada, tambor (`fuse`) na apuração,
pancada na cor de quem levou cada rodada, `KIT_SFX_REVEAL` nos percentuais e
`KIT_SFX_ONBOARD_DONE` na conquista.

## Persistência

Só os ajustes (`div_rod`, `div_rit`). A partida é um duelo de reflexo de ~1
minuto e não sobrevive a fechar/reabrir de propósito. A tela fica acesa
(`power->keep_awake`) da entrada até o resultado.

## Build

```bash
cmake -B build -S . -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake --build build && ctest --test-dir build      # lógica (test_divisa)
kit-cli build . --target xtensa                     # tool.so
```

A lógica pura está em `src/divisa_game.c` (inteiro puro, sem float). O ícone
sai de `scripts/make_icon.py` (`icon.svg` é a fonte de verdade).

## Licença

GPL-3.0, como o resto do catálogo.
