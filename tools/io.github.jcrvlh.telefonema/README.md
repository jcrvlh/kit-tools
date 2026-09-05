# Telefonema

Jogo de reflexo de mesa: "o KIT toca sozinho, atenda no toque certo".

O KIT fica parado na mesa. Depois de tocar em **COMEÇAR**, ele toca uma vez
sozinho o **toque certo** — a referência da rodada. Em seguida começam a
tocar vários toques: alguns são **trotes** (parecidos, mas errados) e um é o
toque certo de novo, em posição sorteada. A tela fica parada o tempo todo —
quem entrega se é trote ou o certo é só o ouvido.

Quem **atender** (chacoalhar o KIT ou tocar a tela) durante o toque **CERTO**
ganha, e o tempo de reação aparece bem grande na tela. Atender cedo demais,
num trote, ou deixar passar o toque certo sem atender é derrota na hora.

## Telas

Titlebar fixa + `lv_tileview` de 3 páginas (começa no JOGO):

- **AJUSTE** — `TROTES` (`SEM` / `POUCOS` / `PADRÃO` / `MUITOS`) e
  `SE NÃO ATENDER A TEMPO` (`PERDE` / `CONTINUA` — desliga o "perder a
  ligação" por timeout). Ambos vão pro Storage.
- **JOGO** — o palco central (toque em qualquer lugar conta como "atender")
  e o botão `COMEÇAR` / `PEGAR!` / `JOGAR DE NOVO` no rodapé, conforme a fase.
  O resultado ("GANHOU"/"PERDEU" + o motivo ou o tempo em ms) aparece bem
  grande.
- **COMO JOGA** — as regras, corpo rolável.

## Toques

Três variantes do "toque certo" (sorteada uma por rodada), e os trotes imitam
o início de qualquer uma delas, cortado antes da hora — no mesmo volume do
toque de verdade, pra não dar pra distinguir só pelo volume.

Chacoalhar liga por `register_shake_callback` (Tools externas não têm
`primary_action` — isso é só das built-in). Permissões: `display`, `input`,
`random`, `storage`, `time`, `audio`, `imu`.

## Como testar

```bash
export KIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake -B build -S . -DKIT_SDK_PATH="$KIT_SDK_PATH" && cmake --build build
kit-cli build --target xtensa
kit-cli pack . -o io.github.jcrvlh.telefonema.kit
```

No KIT: instale o `.kit` pelo Catálogo (ou copie pra `/sdcard/tools/`) e abra
**Telefonema** na Home.

## Licença

Código da Tool sob GPL-3.0 (mesma da infraestrutura).
