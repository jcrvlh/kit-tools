# Vira Certo

**Vira Certo** é um mini-jogo de mesa para o KIT: pontaria giratória. Cada
rodada sorteia **um alvo** — ângulo, eixo e direção — igual pra todo mundo:
_"GIRE 130 À ESQUERDA"_ ou _"INCLINE 40 PRA BAIXO"_. O jogador da vez toca
**PRONTO**, gira ou inclina o KIT tentando parar bem em cima do número, e
segura quieto por um instante. O giroscópio integra o ângulo percorrido e
detecta sozinho que você parou. Quanto mais perto do alvo, mais pontos:
**PERFEITO**, **ÓTIMO**, **QUASE** ou **ERROU**.

A dificuldade sobe a cada rodada — ângulos maiores, menos "redondos" (nada de
múltiplos de 45°), mais chance de virar _inclinar_ em vez de _girar_, e a
tolerância aperta. A 1ª rodada é sempre um aquecimento de 90°; a última passa
de **uma volta inteira**.

Uma dica sonora de "quente/frio" (o mesmo motor `audio → fuse` do
[Pavio](../io.github.jcrvlh.pavio)) sobe conforme você chega perto do alvo e
some quando você para — daí em diante é só o tique da contagem regressiva.

---

## Como Jogar

1. **Ajuste**: escolha quantos jogadores (2–8) e quantas rodadas (3, 5 ou 7).
   Toque em **COMEÇAR**.
2. A tela mostra o alvo. O jogador da vez toca **PRONTO**, gira ou inclina o
   KIT e fica quieto pra confirmar que parou — ou toca **PARAR** pra travar
   na hora.
3. O erro em graus vira uma faixa de pontos. Passa o KIT pro próximo jogador.
4. No fim das rodadas, quem tiver mais pontos no **PLACAR** vence.

---

## Estrutura da Tool

- **Runtime mínimo**: `0.4.0` — primeira Tool do catálogo a usar o
  giroscópio (`kit_api → imu → gyro_start / gyro_rezero / gyro_poll /
  gyro_stop`, em centigraus inteiros).
- **UI**: Brutalist Bauhaus (`kit_theme.h` / `kit_fonts.h`), 4 páginas —
  **AJUSTE**, **JOGO** (palco com o número protagonista), **PLACAR**,
  **COMO JOGA**.
- **Áudio**: dica contínua de quente/frio no `audio → fuse` durante a
  medição; `KIT_SFX_TIMER_TICK` na contagem regressiva; `KIT_SFX_CONFIRM` /
  `KIT_SFX_VETO_FOUL` no resultado.
- **Entradas**: toque na tela (palco inteiro ou botão). Sem gesto de
  chacoalhar — girar/inclinar já é o jogo.
- **Sem float**: toda a aritmética é inteira e em centigraus (o `.so` não
  resolve `__divsf3` no carregador do KIT). O giroscópio devolve
  centigraus inteiros pela própria API.
- **Persistência**: nº de jogadores e de rodadas (`storage`).

## Testar

```bash
pip install <kit>/tools-sdk/cli jsonschema
python scripts/validate_tool.py tools/io.github.jcrvlh.viracerto
cmake -B tools/io.github.jcrvlh.viracerto/build -S tools/io.github.jcrvlh.viracerto \
  -DKIT_SDK_PATH=<kit>/tools-sdk
cmake --build tools/io.github.jcrvlh.viracerto/build   # ./tool.elf (stub, sem UI)
```

No hardware: instale pelo **Catálogo** do KIT (runtime ≥ 0.4.0) e valide
girar, inclinar, a contagem regressiva e o placar.

## Licença

MIT (código da Tool). A infraestrutura do catálogo é GPL-3.0.
