# Fora — `io.github.jcrvlh.fora`

Jogo de **dedução social** para 3–12 pessoas, passando um único KIT de mão em mão.

Todos os jogadores recebem a mesma **palavra secreta** — todos, menos um: o
**FORA**, que só sabe que está fora e precisa disfarçar. Depois de rodadas de
perguntas entre pares, o grupo vota em quem acha que é o FORA. Se acertarem, o
FORA ainda tem uma última chance: adivinhar a palavra entre 4 opções.

## Como jogar

1. **Ajustes** — na tela inicial (PALCO), deslize para a esquerda até AJUSTES:
   número de jogadores (3–12), nomes de 3 letras (opcional — toque no número,
   depois no topo/base de cada letra), rodadas (1 ou 2) e assuntos (ou `MIX`).
   Deslize de volta e toque em **COMEÇAR**.
2. **Distribuição** — o KIT passa de mão em mão; cada jogador revela sua palavra
   em segredo e oculta antes de passar adiante.
3. **Perguntas** — o app sorteia uma cadeia de pares "X pergunta para Y". As
   respostas são faladas, em voz alta, na mesa.
4. **Votação** — toque em quem a mesa acha que é o FORA.
5. **Revelação** — se a maioria acertou o FORA, ele tenta adivinhar a palavra
   entre 4 opções. Senão, o FORA escapa e vence.

Vitória dos jogadores: descobrir o FORA **e** impedir o chute final. Vitória do
FORA: escapar da votação **ou** acertar a palavra.

## Estrutura de telas

Padrão do KIT (ver `kit/docs/design/design-language.md`, Tool DADOS): uma tela
só, titlebar fixa (chip **Voltar** = sair, igual DADOS) + um `lv_tileview`:

```
AJUSTES  ◄──►  PALCO
```

Começa sempre no **PALCO**. Todas as fases (distribuição → revelação →
perguntas → votação → chute → resultado) acontecem no PALCO reconfigurando os
mesmos widgets — nunca destruindo/recriando dentro de um callback (isso trava a
placa). O tileview trava durante a partida e volta a deslizar no RESULTADO
(basta deslizar para AJUSTES para jogar de novo — sem botão "alterar config"
nem "sair").

## Estrutura

| Arquivo | Papel |
| :--- | :--- |
| `src/main.c` | UI e ciclo de vida: titlebar + tileview AJUSTES/PALCO, render por fase (LVGL v9). |
| `src/fora_game.c/h` | Máquina de estados e lógica pura — sem LVGL. Inclui os nomes de jogador. |
| `src/fora_words.c/h` | Banco de palavras: 20 categorias, 50+ palavras cada (CAIXA ALTA, UTF-8). |
| `test_fora.c` | Testes da lógica pura (build nativo). |
| `icon.svg` | Fonte do ícone; `scripts/make_icon.py` gera `icon.png` e `icon.bin`. |

A interface segue o *Brutalist Bauhaus* do KIT: fundo AMOLED preto, tipografia
mono em CAIXA ALTA, accent `KIT_COLOR_RED` (`#C6472F`), botões pill e áreas de
toque grandes. Palavra/frase sempre em `kit_mono_26`; só o "FORA" dramático usa
`kit_display_72`. Os toques são motivos graves e curtos (~110–300 Hz), feitos
com `audio->beep`. Tudo é liberado em `tool_destroy`.

## Testar localmente (build nativo)

```bash
# o KIT Tools SDK vem do repositório principal (jcrvlh/kit)
cmake -B build -S . -DKIT_SDK_PATH=/caminho/para/kit/tools-sdk
cmake --build build

ctest --test-dir build --output-on-failure   # lógica pura (test_fora)
./build/tool.elf                             # roda a Tool em modo stub (sem UI)

# validação de manifest/namespace roda a partir da raiz do catálogo:
python ../../scripts/validate_tool.py tools/io.github.jcrvlh.fora
```

## Empacotar (Xtensa)

```bash
# toolchain xtensa-esp-elf no PATH + headers do LVGL 9.5.x
export KIT_SDK_PATH=/caminho/para/kit/tools-sdk
kit-cli build . --target xtensa      # -> tool.so
kit-cli pack . -o io.github.jcrvlh.fora.kit
kit-cli validate io.github.jcrvlh.fora.kit
```

O ícone (`icon.png`, `icon.bin`) é gerado de `icon.svg` por
`python scripts/make_icon.py` (sem dependências).

## Permissões

`display`, `input`, `storage` (lembra a última config), `random` (sorteio de
palavra, FORA e pares), `audio` (beeps de toque e suspense).

## Licença

- **Banco de palavras** (`src/fora_words.c`): listas originais, escritas para o
  KIT — **CC0 1.0** (domínio público).
- **Código**: GPL-3.0, igual ao catálogo.

Autor: [@jcrvlh](https://github.com/jcrvlh)
