# Soundbox

**Soundbox** é uma mesa de sons de mão para o KIT: uma grade **3×3** de pads;
cada **toque** toca um som, e um som novo **corta** o anterior — sem polifonia,
no ritmo de uma soundbox de zoeira. Chacoalhar o KIT retoca o último pad.

A Soundbox já vem com o banco **Exemplo** (7 sons, embutido no `.kit`). Os seus
bancos vêm do **cartão microSD**: cada subpasta de `/soundbox/` na raiz do cartão
é um banco (um conjunto de até 9 sons). A Tool só lê — nunca escreve no cartão.

---

## Como usar

1. **Monte um banco no conversor web**
   ([jcrvlh.github.io/kit/soundbox.html](https://jcrvlh.github.io/kit/soundbox.html)):
   solte seus áudios (MP3, WAV, o que tiver), dê nome e cor a cada pad. O
   conversor deixa cada áudio no formato que o KIT toca (**WAV PCM 16-bit, mono,
   16 kHz**), nivela o volume e avisa se algum arquivo não serve.
2. **Baixe a pasta pronta** — ou grave direto no cartão com o KIT em
   **Ajustes → Modo pen drive**. A pasta vai para `/soundbox/<nome>/` na raiz.
3. **Abra a Soundbox**: página **PADS** (central) é o palco. Deslize para
   **BANCOS** (esquerda) para trocar de conjunto e ajustar o volume, ou para
   **ADICIONAR SONS** (direita) para o passo a passo e o QR do conversor.

O banco escolhido e o volume são lembrados entre sessões.

---

## `banco.json` (opcional)

Cada banco pode trazer um `banco.json` que dá nome, cor e ordem aos pads. Tudo é
opcional — sem ele, a Tool lista os `*.wav` em ordem alfabética com o nome do
arquivo como rótulo.

```json
{
  "nome": "Meus Sons",
  "cor": "verde",
  "pads": [
    { "arquivo": "buzina.wav", "rotulo": "BUZINA", "cor": "vermelho" },
    { "arquivo": "tada.wav",   "rotulo": "TADÁ" }
  ]
}
```

- `cor`: `vermelho` · `azul` · `amarelo` · `verde`. Pad sem `cor` herda a do banco.
- A ordem de `pads[]` é a ordem da grade; `*.wav` fora da lista entram depois
  (alfabético). Entrada cujo arquivo não existe no disco é ignorada.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` / `kit_fonts.h`), tileview de 3 páginas
  (BANCOS ◄─► PADS ◄─► ADICIONAR SONS), abre em PADS.
- **Áudio**: `audio->play_sample()` com o caminho do `.wav` no cartão; retrigger
  ao tocar outro pad. Flash branco de 140 ms no pad tocado (o som pode ser baixo).
- **Bancos**: `opendir`/`readdir`/`stat` varrem `/sdcard/soundbox/<banco>/` (do
  usuário) **e** `<data_path>/assets/` (o banco Exemplo, extraído do `.kit` pelo
  instalador em `/sdcard/tools/io.github.jcrvlh.soundbox/assets/`). `banco.json`
  é lido com um parser mínimo embutido (o `.so` não tem cJSON). Exige
  **runtime ≥ 0.8.0** (quando esses símbolos entraram na superfície das Tools).
- **Exemplo**: 7 sons do Pixabay (~350 KB no `.kit` — abaixo do teto de 768 KB do
  Catálogo). Créditos na página ADICIONAR SONS e em `assets/CREDITOS.txt`.
- **Entradas**: toque no pad (`LV_EVENT_SHORT_CLICKED`, para deslizar não tocar),
  chacoalhar (retoca o último) e o PWR físico.
- **Persistência**: `storage` — `sb_bank` (nome do banco escolhido) e `sb_vol` (volume).

Nasceu built-in (componente `kit_soundbox` no firmware) e migrou para o catálogo
com o runtime 0.8.0.
