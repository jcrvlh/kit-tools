# Testa

Mini-jogo de mesa estilo *Heads Up!* para o KIT.

Uma pessoa segura o KIT encostado na testa, com a tela virada pra roda. A roda dá
dicas — falando, cantando ou gesticulando, como combinarem — e a pessoa tenta
adivinhar a palavra. **Inclina o KIT pra baixo** (tela pro chão) quando acerta;
**pra cima** (tela pro teto) quando quer passar. Quando o TEMPO acaba, o KIT
mostra quantas ela fez — quem valida cada palpite é a roda (filosofia do Bingo).

## Telas

Titlebar fixa + `lv_tileview` de 3 páginas (começa no JOGO):

- **AJUSTE** — TEMPO (`60S / 90S / 120S / OFF`) e BARALHO (FILMES & SÉRIES,
  CELEBRIDADES, ANIMAIS, GÍRIAS, OBJETOS & LUGARES, MIX). Só os ajustes vão pro
  Storage.
- **JOGO** — a palavra grande virada pra roda, a barra de tempo e a dica do
  gesto. Sem botões de acerto/passe: a mecânica é inclinar. Entre as cartas, um
  flash de tela cheia (verde ACERTOU / vermelho PASSOU).
- **COMO JOGA** — as regras, corpo rolável.

Tempo esgotado → overlay amarelo TEMPO com o placar da vez. O 1º toque no overlay
cala o alarme; a vez só passa no botão PASSAR A VEZ.

## Gesto de inclinar

Usa `kit_api.imu->register_tilt_callback` (SDK ≥ 0.2.0, `min_runtime` `"0.2.0"`).
O Runtime só faz o polling do gesto enquanto o callback está registrado. Os
limiares (ângulo, debounce, filtro de solavanco) são calibrados no firmware
(`kit_imu.c`), não pela Tool.

## Como testar

```bash
export KIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake -B build -S . -DKIT_SDK_PATH="$KIT_SDK_PATH" && cmake --build build   # build nativo (stubs)
kit-cli build --target xtensa                                               # tool.so pro KIT
kit-cli pack . -o io.github.jcrvlh.testa.kit
```

No KIT: instale o `.kit` pelo Catálogo (ou copie pra `/sdcard/tools/`) e abra
**Testa** na Home.

## Licença

Código da Tool sob GPL-3.0 (mesma da infraestrutura). Baralhos: cultura pop /
domínio público, curadoria do autor.
