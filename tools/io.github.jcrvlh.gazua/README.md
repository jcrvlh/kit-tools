# Gazua — `io.github.jcrvlh.gazua`

Jogo de dedução solo inspirado no tabuleiro *Turing Machine*. Tem um código
de 3 números escondido, um em cada forma (triângulo, quadrado e círculo, de
1 a 5), e 4 **verificadores**. Cada verificador tem **uma regra secreta**,
tirada de uma lista que você vê, e só responde **SIM** ou **NÃO**. Monte um
código, teste até 3 verificadores por rodada e arrisque quando souber.

O que dá graça ao jogo: cada resposta corta códigos **e** corta regras. A
dedução cruza as duas coisas.

## Páginas

```
AJUSTE  ◄──►  CÓDIGO  ◄──►  TESTES  ◄──►  COMO JOGA
```

Abre sempre em **CÓDIGO**. O jogo ocupa duas páginas para que todo alvo de
toque tenha 80 px (`KIT_TOUCH_TARGET_COMFORTABLE`). Por cima delas abrem telas
cheias com a própria barra de título (o ◀ fecha a tela): **VERIFICADOR**,
**NOTAS**, **ARRISCAR** e **RESULTADO**.

- **AJUSTE**
  - **Regras:** FÁCIL (só regras sobre uma forma ou um par de formas) ou
    DIFÍCIL (inclui contagem, menor/maior, repetição e ordem).
  - **Puzzle:** DO DIA (o mesmo em todo KIT no mesmo dia, semente da data) ou
    LIVRE. Sem data no relógio, ou se o do dia já foi jogado, o puzzle é livre
    e um aviso diz isso.
  - **Ajuda:** risca sozinho as regras que algum teste já contradiz e, em
    ARRISCAR, confere o palpite contra os seus testes. Não olha a resposta.
    Vem desligada.
  - **Histórico:** jogados, abertos e o melhor placar (rodadas e testes).
  - No meio de uma partida, mudar Regras ou Puzzle só vale no próximo
    puzzle. Nada se perde por um toque sem querer.
- **CÓDIGO:** "RODADA n · x/3" e a dica "TESTES ▸", os 3 discos de 104×124
  (o toque avança 1→5; o código trava no 1º teste da rodada), [NOTAS] e
  [NOVA RODADA] com 162×80 cada, e [ARRISCAR] com 336×80.
- **TESTES:** os 4 verificadores em linhas de 336×80: tema da carta à
  esquerda, resultado da rodada à direita.
- **COMO JOGA:** as regras, curtas.

### VERIFICADOR

O título é o tema da carta. Embaixo vêm as regras possíveis em linhas de
336×80 (um toque risca, outro desrisca; com Ajuda, as contraditas aparecem
como AUTO) e o botão **TESTAR △2 □4 ○3**, com o código da rodada numa
pastilha escura (o triângulo amarelo sumiria no fundo amarelo). As 3 regras
de uma carta comum cabem sem rolar. O histórico de cada verificador fica em
NOTAS > TESTES. A resposta aparece num carimbo de tela cheia, **SIM** verde
ou **NÃO** vermelho, com glifo além da cor.

### NOTAS

- **TESTES:** uma linha por verificador e uma coluna por rodada, com o código
  empilhado no topo. A rodada mais recente vem primeiro, e as antigas ficam
  rolando pro lado.
- **NÚMEROS:** uma forma por vez e um botão por número. Cada toque alterna
  entre riscado, certo e limpo. O que estiver marcado como CERTO já abre
  preenchido no ARRISCAR.

### ARRISCAR

A própria tela é a confirmação: são 2 toques no total. Ela mostra os discos
editáveis, o placar que você faria se acertar e, com Ajuda, se o palpite bate
com os seus testes. O botão ARRISCAR só acende **0,6 s depois** de a tela
abrir, então um toque duplo não arrisca sem querer. Um palpite só: errou,
acabou.

### RESULTADO

Uma abertura de ~1,8 s, que um toque pula: a tela é inundada de verde e o
arco do cadeado sobe (**ABERTO**), ou a tela fica vermelha e o cadeado treme
(**TRAVOU**). Depois vem o resumo, que rola inteiro: a faixa do resultado, o
código (ou ERA × VOCÊ em colunas, com selo ✓/X em cada forma) e a regra
secreta de cada verificador. No fim fica o botão NOVO PUZZLE.

## O puzzle

São 25 cartas (`gazua_game.c`), com 2 a 4 regras cada. O gerador sorteia 4
cartas e só aceita o conjunto se, entre **todas** as combinações de uma regra
por carta, **exatamente uma** fecha um código único sem nenhum verificador
sobrando. É isso que garante solução por dedução: se uma regra parece não
servir pra nada, ela não é a regra certa. Com só 125 códigos, cada regra
vira uma máscara de 125 bits e a busca leva poucos milissegundos.

O gerador é determinístico por semente (mulberry32). O puzzle do dia sai
igual em todo KIT, e a partida salva é reconstruída a partir de semente +
dificuldade. **A ordem das cartas faz parte da semente:** mudar a tabela
muda os puzzles do dia.

## Acessibilidade

Feito para o KIT na mão, perto do rosto:

- texto necessário para jogar em 26–28 px (`kit_mono_26`, `kit_sans_28`) e
  números em `kit_display_44`;
- alvos de toque de 80 px (`KIT_TOUCH_TARGET_COMFORTABLE`), com 8 a 12 px
  de vão entre vizinhos; o ◀ é o chip padrão do shell (56 px, com 12 px de
  toque extra em volta);
- cor nunca vem sozinha: a forma diz quem é quem, e SIM/NÃO têm glifo.

## Persistência

`gz_game` guarda a partida em andamento (semente, dificuldade, modo, dia e as
rodadas: código + 4 resultados). `gz_strike` e `gz_grid` guardam os riscos e
a grade de números. Os ajustes ficam em `gz_diff`, `gz_mode` e `gz_assist`, e
o histórico em `gz_played`, `gz_won`, `gz_bestr`, `gz_bestt` e `gz_dday`. A
gravação é em lote (a cada 1,5 s, se mudou) e ao sair. O ◀ do shell sai
direto: ao abrir de novo, a partida continua.

## Notas de implementação

- Nada é apagado dentro do callback do objeto tocado. O que muda de
  estrutura (as cartas de um puzzle novo, o conteúdo de uma tela) é
  reconstruído num `lv_timer` de um disparo.
- Sem `lv_anim` na tabela de símbolos: a abertura do resultado anda num
  `lv_timer` pelo relógio.
- As áreas que rolam continuam CLICKABLE: o LVGL só encontra quem rola a
  partir de um alvo clicável.
- As formas são bitmaps A8 em 3 tamanhos (`scripts/make_shapes.py`, mesma
  geometria da Repete). O ícone sai de `scripts/make_icon.py`, com
  `icon.svg` como fonte de verdade.
- `memcmp` não está garantido no loader e foi evitado. Todos os símbolos
  indefinidos do `tool.so` estão em `kit_tool_symbols.c`.

## Build

```bash
cmake -B build -S . -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake --build build && ctest --test-dir build      # lógica (test_gazua)
kit-cli build . --target xtensa                     # tool.so
```

O `test_gazua` confere 800 puzzles (FÁCIL e DIFÍCIL) contra uma
implementação independente e ingênua, sem máscaras: solução única, nenhum
verificador sobrando, regra secreta correta e determinismo.

## Licença

GPL-3.0, como o resto do catálogo.
