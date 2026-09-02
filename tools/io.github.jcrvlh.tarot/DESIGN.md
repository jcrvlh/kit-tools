# Tarot — Design da Tool (`io.github.jcrvlh.tarot`)

> Documento de design da V1. Vive no catálogo (`kit-tools/tools/io.github.jcrvlh.tarot/`),
> instalável como pacote `.kit`. Trilha **Comunidade** (namespace `io.github.jcrvlh.*`).

## 1. Conceito

Tarô digital focado em **tiragem rápida + interpretação em texto**. Não é uma
simulação de baralho físico: não há arte de carta, não há "virar carta" com o
dedo. A Tool sorteia, mostra **nome + significado**, e sai da frente.

Uma consulta é um ritual curto de três tempos:

```
Tarô → escolher tiragem → concentrar na pergunta → tirar → resultado
```

Para três cartas, o resultado é revelado posição a posição:

```
Tarô → Três cartas → concentrar → Passado → Presente → Futuro → Resultado
```

## 2. Escopo da V1

| Entra na V1 | Fica de fora (futuro) |
| :--- | :--- |
| Baralho completo: 78 cartas (22 Maiores + 56 Menores) | Arte / imagem das cartas |
| Nome, palavra-chave, significado normal e invertido | Tiragens customizadas (cruz celta, etc.) |
| Tiragem de **uma carta** | Diário / histórico de tiragens |
| Tiragem de **três cartas** (Passado / Presente / Futuro) | Carta do dia / notificação diária |
| Cartas invertidas: liga/desliga | Múltiplos baralhos com conteúdo real |
| Seleção de baralho (1 baralho em V1, UI já preparada p/ N) | Leitura contextual por posição (texto próprio p/ passado/presente/futuro) |
| Chacoalhar **ou** tocar para tirar | Compartilhar / exportar a tiragem |

## 3. Identidade visual

Segue a linguagem **Brutalist Bauhaus** do KIT (`kit_theme.h`, `kit_fonts.h`),
mesmos padrões da Tool `Fora`:

- Fundo AMOLED preto (`KIT_COLOR_BG`), texto paper (`KIT_COLOR_TEXT`), rótulos
  em `KIT_COLOR_TEXT_MUTED`.
- Tipografia mono CAIXA ALTA (`kit_mono_16/20/26`); nome da carta em
  `kit_display_44`.
- **Accent: azul** (`KIT_COLOR_BLUE` `#2C3CC4`) — cor do card na Home e do botão
  de ação principal. (Azul = "informação / mística" na paleta Bauhaus do KIT.)
- `home_icon: "card"`, `kind: "tool"`.
- Botões pill no rodapé, alvos de toque ≥ 80 px, `lv_obj_set_ext_click_area`.
- Sem caixas decorativas em volta de texto em telas pequenas (regra de design do
  KIT): o texto respira direto no preto.
- As fontes do KIT trazem Latin-1 + `U+2022`, mas **não** o travessão (`—`) nem a
  seta (`→`) — sem tratamento eles viram um retângulo vazado. `tr()` em `main.c`
  troca `—`/`–` por hífen e `→` pelo chevron FontAwesome na hora de renderizar
  (o texto original do baralho, cheio de `—`, fica intacto no fonte).

### Naipe → forma/cor (marcação leve, opcional na V1)

Cada naipe herda um elemento e uma cor da paleta. Exibido como rótulo colorido
(`PAUS · FOGO`); o glifo geométrico é *nice-to-have* (as fontes do KIT só trazem
▲ ● ■ como glifos FontAwesome).

| Naipe | Elemento | Cor | Glifo |
| :--- | :--- | :--- | :--- |
| Paus | Fogo | `KIT_COLOR_YELLOW` | ▲ |
| Copas | Água | `KIT_COLOR_BLUE` | ● |
| Espadas | Ar | `KIT_COLOR_TEXT` | ▲ (rotacionado 180°) ou barra |
| Ouros | Terra | `KIT_COLOR_RED` | ■ |
| Arcano Maior | — | `KIT_COLOR_BLUE` (accent) | as três primitivas |

### Orientação da carta

- **Direita:** nada de especial.
- **Invertida:** badge `INVERTIDA` em accent, abaixo do nome. Não se rotaciona o
  texto do nome (ilegível); usa-se o rótulo.

## 4. Telas e fluxo

Todas as telas: 368 × 448, `F_PAD 16`, conteúdo útil 336 px. Telas de resultado
**rolam na vertical** (flex column + scroll, como `build_result` da Fora).

### 4.1 Menu (entrada da Tool)

```
┌──────────────────────────────┐
│                              │
│  TAROT                       │  kit_display_44, accent
│  leitura & reflexão          │  kit_mono_16, muted
│                              │
│  ┌────────────────────────┐  │
│  │  UMA CARTA             │  │  linha surface, h≈88
│  │  uma pergunta          │  │
│  └────────────────────────┘  │
│  ┌────────────────────────┐  │
│  │  TRÊS CARTAS           │  │
│  │  passado · presente…   │  │
│  └────────────────────────┘  │
│                              │
│      [ AJUSTES ]   [ SAIR ]  │  chips surface, rodapé
└──────────────────────────────┘
```

### 4.2 Concentração ("pense na questão")

```
┌──────────────────────────────┐
│                              │
│  CONCENTRE-SE NA SUA         │  kit_sans_28, TEXT — o foco da tela
│  PERGUNTA                    │
│                              │
│   chacoalhe o KIT            │  kit_sans_22, muted
│   ou toque na tela           │
│                              │
│      [    TIRAR CARTA    ]   │  pill azul (só nesta tela)
└──────────────────────────────┘
```

- Sem as primitivas ▲ ● ■: aquilo é a logo do KIT, não do Tarot.
- **Shake** (`imu.register_shake_callback`) → dispara a tiragem.
- **Tap** na tela ou no botão → dispara a tiragem.
- Ajustes fica no tile à **esquerda**; a tela abre na PRINCIPAL. O botão-pílula
  some quando se desliza para os Ajustes.
- Para três cartas, o botão diz `TIRAR AS TRÊS`.

### 4.3 Embaralhar (animação, ~1,0–1,4 s)

Reaproveita o padrão de *reveal* da Fora: o nome de uma carta "pisca"
aleatoriamente, desacelerando, com `KIT_SFX_*`/beeps curtos e discretos. Ao
travar, transiciona para o resultado. Para três cartas: um embaralho só, depois
revelação sequencial.

Som: escala **Frígia dominante de Mi** ("Hijaz" — `E F G# A B C D E`, o modo de
sonoridade oculta/esotérica, com o salto de 2ª aumentada `F→G#`). No embaralho
ela **desce** devagar (E5 → G#4), uma nota a cada ~3 ticks, as últimas segurando
mais — uma ladainha ritual que trava no G#4 suspenso. A revelação resolve com um
floreio Hijaz que sobe e abre (carta normal) ou desce e fecha (invertida),
última nota longa. Tudo ≥ 300 Hz, 70–300 ms — sem o "estouro" de bipes graves e
curtos. Deslizar entre tiles/telas é **mudo** de propósito.

### 4.4 Resultado — uma carta (rolável)

```
┌──────────────────────────────┐
│  A CARTA                     │  mono_16, muted
│                              │
│  O ENFORCADO                 │  kit_display_44, TEXT
│  XII · ARCANO MAIOR          │  mono_16, muted
│  INVERTIDA                   │  mono_20, accent  (só se invertida)
│                              │
│  A carta da pausa e da       │  `about` — o que a carta é, p/ quem
│  rendição: parar, se         │  não conhece tarot. kit_mono_16,
│  entregar, olhar de outro    │  wrap, muted
│  ângulo.                     │
│                              │
│  PAUSA · ENTREGA · LIMITE    │  palavra-chave, muted
│                              │
│  Um período de pausa         │  significado (upright/reversed):
│  forçada. Ver a situação     │  kit_mono_16, wrap, TEXT
│  de outro ângulo antes de…   │  (~4–7 linhas; rola se precisar)
│  ───────────────────────     │  KIT_COLOR_LINE
│  [     TIRAR OUTRA      ]    │  pill azul
│  [        VOLTAR         ]   │  chip surface
└──────────────────────────────┘
```

- Menor: `TRÊS DE COPAS` / `VALETE DE OUROS`; subtítulo `COPAS · ÁGUA`.

### 4.5 Resultado — três cartas

Revelação posição a posição. Cada posição é uma tela de resultado (layout da
4.4) com cabeçalho `PASSADO` / `PRESENTE` / `FUTURO` e botão de avanço
(`PRESENTE →`, `FUTURO →`, `RESULTADO →`).

**Leitura contextual (V1):** cada posição mostra o significado base da carta
(`upright`/`reversed`, conforme a orientação sorteada) **seguido de uma frase de
enquadramento temporal** própria da carta (`as_past` / `as_present` /
`as_future`, ver §6). São 78 × 3 frases curtas extras — não 78 × 3 × 2, porque a
lente de posição é escrita de forma independente da orientação.

Tela final **Resultado** (rolável, resumo compacto):

```
┌──────────────────────────────┐
│  RESULTADO                   │
│                              │
│  PASSADO                     │  mono_16, muted
│  O Enforcado · invertido     │  mono_20, TEXT
│                              │
│  PRESENTE                    │
│  Três de Copas               │
│                              │
│  FUTURO                      │
│  O Sol                       │
│  ───────────────────────     │
│  [    NOVA TIRAGEM     ]     │
│  [        VOLTAR         ]   │
└──────────────────────────────┘
```

### 4.6 Ajustes (rolável)

```
┌──────────────────────────────┐
│  AJUSTES                     │
│                              │
│  TIRAGEM                     │  mono_16, muted
│  [ UMA CARTA ] [ TRÊS ... ]  │  dois chips, seleção em accent
│                              │
│  BARALHO                     │
│  [ SÓ MAIORES ] [ 78 CARTAS ]│  22 Arcanos Maiores × baralho completo
│                              │
│  CARTAS INVERTIDAS           │
│  [ SIM ] [ NÃO ]             │
│                              │
│  ─────────────────────────   │
│  Tarot é entretenimento e    │  "Sobre": só estas linhas muted no
│  reflexão — não previsão.    │  rodapé de Ajustes, sem tela própria
│  v1.1.0                      │
│                              │
│  [        VOLTAR         ]   │
└──────────────────────────────┘
```

O "Sobre" **não** é uma tela nem um item de menu: são as três linhas mudas no
fim da lista de Ajustes (disclaimer + versão).

## 5. Lógica de tiragem

Módulo `tarot_draw.c/.h` — **sem dependência de LVGL nem da API do KIT**
(recebe um `int (*rng)(int lo, int hi)`), para ser testável no build nativo
(`test_tarot.c`, como `test_fora.c`).

```c
typedef struct {
    uint8_t  index;      /* 0..77 no baralho */
    bool     reversed;   /* orientação sorteada */
} tarot_pick_t;

/* Sorteia n cartas distintas (sem reposição) via Fisher–Yates parcial.
 * Se reversed_enabled, cada carta tem TAROT_REVERSED_CHANCE de vir invertida. */
void tarot_draw(tarot_pick_t *out, int n, bool reversed_enabled,
                int (*rng)(int lo, int hi));
```

- Sorteio **sem reposição**: nunca repete carta numa mesma tiragem.
- `TAROT_REVERSED_CHANCE` = 50 % — moeda justa por carta. É só frequência: não
  muda a mecânica, só quão comum é sair uma invertida. (Alguns leitores usam
  ~30 % para dar mais "peso" à invertida; ficamos no 50 % por simplicidade e
  ausência de viés.)
- Fonte de aleatoriedade: `ctx->api->random->range` (TRNG de hardware, sem seed).
- O shake não altera o resultado — é só o gatilho/ritual.

## 6. Dados do baralho

Módulo `tarot_deck.c/.h` — array `const` de 78 entradas:

```c
typedef struct {
    const char *name;        /* "O Enforcado", "Três de Copas"  (≤ 24 ch) */
    const char *arcana;      /* "XII · Arcano Maior" | "Copas · Água"     */
    const char *about;       /* o que a carta É, em linguagem simples,    */
                             /* p/ quem não conhece tarot — neutro quanto */
                             /* à orientação (1–2 frases, ≤ ~160 ch)      */
    const char *keywords;    /* "Pausa · Entrega · Limite"  (≤ 3 termos)  */
    const char *upright;     /* significado normal   (≤ ~220 ch)          */
    const char *reversed;    /* significado invertido (≤ ~220 ch)         */
    const char *as_past;     /* lente "Passado"  (1 frase, ≤ ~120 ch)     */
    const char *as_present;  /* lente "Presente" (1 frase, ≤ ~120 ch)     */
    const char *as_future;   /* lente "Futuro"   (1 frase, ≤ ~120 ch)     */
} tarot_card_t;

extern const tarot_card_t TAROT_DECK[78];
```

Ordem: 0–21 Maiores (O Louco … O Mundo), depois Paus Ás–Rei (22–35),
Copas (36–49), Espadas (50–63), Ouros (64–77).

**Estimativa de tamanho:** ~78 × 800 B ≈ 62 KB de `.rodata` (incl. `about` + as
3 lentes de posição) → `tool.so` ~140–180 KB. Muito abaixo do limite de 7 MB do
`.kit` e do orçamento de PSRAM.

### 6.1 As três camadas de texto

Para quem não conhece tarot, cada carta se explica em camadas, da mais geral
para a mais aplicada:

| Campo | Responde | Muda com orientação? | Muda com posição? |
| :--- | :--- | :---: | :---: |
| `about` | "o que é essa carta?" | não | não |
| `keywords` | "em 3 palavras?" | não | não |
| `upright` / `reversed` | "o que ela diz pra mim agora?" | **sim** | não |
| `as_past` / `as_present` / `as_future` | "e no lugar dela na tiragem?" | não | **sim** |

- **Uma carta:** mostra `about` → `keywords` → `upright`/`reversed`.
- **Três cartas:** cada posição mostra `about` → `upright`/`reversed` → a lente
  daquela posição (`as_past`/`as_present`/`as_future`).

### 6.2 Diretrizes de conteúdo (texto)

- **Texto 100 % original**, escrito para o KIT. Significados de tarô como
  *conceito* não são protegíveis, mas a **redação** de livros/baralhos modernos
  é. A arte Rider-Waite-Smith (1909) é domínio público — não a usamos mesmo
  assim (Tool é sem imagem).
- Tom: conciso, reflexivo, **não fatalista**, não "vai acontecer X". PT-BR.
- Sem afirmações sobre saúde, dinheiro ou decisões jurídicas/médicas.
- Limites de caractere respeitam a tela (ver struct). Nome sempre cabe em
  `kit_display_44` a 336 px sem quebrar feio — nomes longos ("A Roda da
  Fortuna") em `kit_mono_26`.
- Licença do conteúdo: **CC0** (declarada no `README.md`), infra sob GPL-3.0.

## 7. Persistência (`storage`)

| Chave | Tipo | Default | Uso |
| :--- | :--- | :--- | :--- |
| `reversed_on` | i32 | `1` | Cartas invertidas ligadas |
| `deck` | str | `"kit"` | Baralho selecionado (id interno) |

## 8. Permissões do manifesto

```json
"permissions": ["display", "input", "random", "storage", "audio", "imu"]
```

| Permissão | Para quê |
| :--- | :--- |
| `display` | tela raiz LVGL |
| `input` | tap nos botões, swipe-right = voltar |
| `random` | TRNG do sorteio |
| `storage` | as 2 chaves de ajustes |
| `audio` | ticks do embaralho + som de revelação |
| `imu` | chacoalhar para tirar |

`system` (exit) está sempre disponível, não precisa de permissão. **Não** usa
`time`, `power` nem `network`.

## 9. `manifest.json` proposto

```json
{
  "manifest_version": 1,
  "id": "io.github.jcrvlh.tarot",
  "name": "Tarot",
  "version": "1.0.0",
  "version_code": 1,
  "min_runtime": "0.1.0",
  "max_runtime": "1.0.0",
  "author": "jcrvlh",
  "description": "Tiragens de tarô com interpretação em texto — uma e três cartas, com cartas invertidas.",
  "icon": "icon.bin",
  "kind": "tool",
  "accent": "#2C3CC4",
  "home_icon": "card",
  "entry_point": "tool.so",
  "arch": "xtensa-esp32s3",
  "permissions": ["display", "input", "random", "storage", "audio", "imu"],
  "api_level": 1,
  "assets": []
}
```

## 10. Estrutura de arquivos

```
tools/io.github.jcrvlh.tarot/
├── manifest.json
├── CMakeLists.txt        # build nativo via KIT_SDK_PATH (padrão do catálogo)
├── icon.svg              # fonte de verdade do ícone (carta + totem Bauhaus)
├── icon.png              # 240×240, gerado — cópia em /icons/ p/ o catálogo web
├── icon.bin              # 64×64 LVGL v9 (ARGB8888), gerado — asset do .kit
├── README.md             # o que faz, como testar, licença CC0 do conteúdo
├── DESIGN.md             # este documento
├── scripts/
│   └── make_icon.py      # rasteriza icon.svg → icon.png + icon.bin (só stdlib)
├── src/
│   ├── main.c            # ciclo de vida + telas/UI (LVGL v9)
│   ├── tarot_draw.c/.h   # sorteio (puro, testável)
│   └── tarot_deck.c/.h   # dados das 78 cartas
└── test_tarot.c          # teste nativo do sorteio (sem reposição, chance de inversão)
```

### Ícone

`home_icon: "card"` desenha o glifo geométrico do card na Home, tingido pelo
`accent` azul (o firmware ainda não renderiza `icon.bin`). O `icon.svg` é uma
carta preta com as três primitivas Bauhaus empilhadas — triângulo amarelo,
círculo azul, quadrado vermelho — como uma "figura de arcano" montada com o
alfabeto visual do KIT. `scripts/make_icon.py` gera `icon.png` (catálogo) e
`icon.bin` (pacote) da mesma geometria.

## 11. Checklist de implementação

- [x] `tarot_deck.c` — 78 cartas com texto original: `about` + `keywords` +
      `upright`/`reversed` + 3 lentes de posição. Nenhum campo acima do limite.
- [x] `tarot_draw.c` + `test_tarot.c` — sorteio sem reposição + inversão
- [x] `main.c` — telas: Menu, Concentração, Embaralho, Resultado 1-carta,
      Resultado 3-cartas (3 posições + resumo), Ajustes
- [x] Shake handler + fallback tap
- [x] Persistência das 2 chaves
- [x] `manifest.json`, `CMakeLists.txt`, `README.md`, `icon.bin` (placeholder)
- [x] `python scripts/validate_tool.py tools/io.github.jcrvlh.tarot`
- [x] build nativo + `./build/test_tarot` (28 checagens de sorteio/baralho)
- [ ] testar UI no hardware · arte de ícone real
- [ ] PR no `kit-tools` → CI valida, compila, empacota, assina, publica

## 12. Decisões fechadas

1. **Nome exibido:** `Tarot`.
2. **Chance de inversão:** 50 % (moeda justa por carta).
3. **Três cartas:** leitura contextual — significado base + lente de posição
   (`as_past`/`as_present`/`as_future`) por carta.
4. **"Sobre":** rodapé de Ajustes, sem tela própria.
4b. **`about`:** toda carta traz uma descrição neutra do que ela é, para quem
   não conhece tarot (ver §6.1).
5. **Lista das 78 cartas:** ver §13 — a aprovar.

## 13. Lista das 78 cartas (nomenclatura PT-BR — a aprovar)

### Arcanos Maiores (0–XXI)

| # | Nome | # | Nome |
| :-- | :-- | :-- | :-- |
| 0 | O Louco | XI | A Justiça |
| I | O Mago | XII | O Enforcado |
| II | A Sacerdotisa | XIII | A Morte |
| III | A Imperatriz | XIV | A Temperança |
| IV | O Imperador | XV | O Diabo |
| V | O Hierofante | XVI | A Torre |
| VI | Os Amantes | XVII | A Estrela |
| VII | O Carro | XVIII | A Lua |
| VIII | A Força | XIX | O Sol |
| IX | O Eremita | XX | O Julgamento |
| X | A Roda da Fortuna | XXI | O Mundo |

Ordem RWS: **VIII = A Força**, **XI = A Justiça**.
Alternativas de nome possíveis: II *A Papisa*, V *O Papa*, XX *O Juízo*.

### Arcanos Menores — 4 naipes × 14 cartas

- **Naipes:** Paus · Copas · Espadas · Ouros
- **Postos:** Ás, Dois, Três, Quatro, Cinco, Seis, Sete, Oito, Nove, Dez,
  Valete, Cavaleiro, Rainha, Rei

Nome de cada carta no formato `<Posto> de <Naipe>` — ex.: `Ás de Paus`,
`Sete de Copas`, `Cavaleiro de Espadas`, `Rainha de Ouros`. São 56 (14 × 4).

Índices no `TAROT_DECK`: 0–21 Maiores · 22–35 Paus · 36–49 Copas ·
50–63 Espadas · 64–77 Ouros (cada naipe na ordem de postos acima).
