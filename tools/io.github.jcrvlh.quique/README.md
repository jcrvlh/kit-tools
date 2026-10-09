# Quique — `io.github.jcrvlh.quique`

Ping-pong **sozinho** contra a parede, controlado **inclinando o KIT**. A cada
10 rebatidas o jogo pausa e abre **3 cartas**: cada uma traz um **bônus e um
ônus juntos**, e os efeitos **acumulam** até o fim. Com 60 pontos, sua raquete
é uma gambiarra de escolhas suas. Três vidas; acabou, o placar entra (ou não)
no top-5.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA  ◄──►  CARTAS
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Sensibilidade:** suave (35° até a borda), normal (25°) ou viva (15°).
  - **Direção da raquete:** normal ou invertida (ver *Giroscópio*).
- **JOGO:** a mesa (protagonista), o recorde, o top-5 e **COMEÇAR**.
- **COMO JOGA:** as regras.
- **CARTAS:** o banco inteiro, com bônus e ônus de cada carta.

## A partida

1. **Calibração:** "SEGURE O KIT RETO" por ~1 s; o giroscópio zera e essa
   posição vira o centro.
2. **Controle:** a inclinação lateral define a **posição** da raquete (não a
   velocidade). Zona morta pequena (0,8°) pra mão não tremer a raquete.
3. **Saque:** a bola fica ~0,7 s na raquete e sai pra cima, com um ângulo
   sorteado de até 15°.
4. **Rebatida:** vale 1 ponto × o multiplicador. O ângulo de volta depende de
   onde a bola bateu (centro = reto, borda = até 60°). A parede de cima devolve
   sempre. A bola acelera 6% a cada 10 rebatidas (teto de +60%).
5. **Vida:** a bola passou da raquete, perde 1 de 3 e volta pro saque.
6. **Escolha:** a cada 10 rebatidas, a mesa some e aparecem 3 cartas abertas
   (4 com a Ganância). Os toques são ignorados nos primeiros 0,7 s, pra
   ninguém escolher sem querer. Não dá pra pular. Depois da escolha, a bola
   volta parada na raquete.
7. **Pausa:** toque no placar, no topo. **CONTINUAR** dá 0,8 s ("VAI!") antes
   de soltar a bola; **ENCERRAR** vai pro fim com o placar valendo.
8. **Fim:** placar grande, a posição no top-5 (ou o recorde) e **DE NOVO** /
   **SAIR**.

## Acúmulo

- **Carta repetida reforça** o efeito (2× Tábua = raquete ainda maior). Cada
  carta tem um máximo de cópias por partida (de 1 a 3).
- **Todo efeito tem teto e piso:** raquete de 40% a 220%, bola de raio 4 a
  18 px, velocidade de 30% a 270%, multiplicador até ×8.
- **Multiplicadores somam, não multiplicam:** ×2 com ×2 dá ×3. Isso mantém o
  top-5 comparável.
- **Faxina e Exorcismo** existem pra evitar a espiral da morte. Só aparecem
  quando há ônus pra tirar: 1 ou mais pra Faxina; 3 ou mais e pelo menos 2
  vidas pro Exorcismo.
- **Bônus de efeito imediato** (vidas, escudo, cargas) não são devolvidos se a
  Faxina tirar o bônus da carta depois.
- A faixa abaixo do placar mostra as cargas (CORTADA / FREIO) e as cartas
  ativas.

## Banco de cartas (29)

`+` = bônus, `-` = ônus. O número é o máximo de cópias por partida.

### Raquete

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Tábua | raquete 40% maior | bola 15% mais rápida | 3 |
| Agulha | pontos em dobro | raquete 35% menor | 2 |
| Ímã | a bola gruda ~0,5 s (você leva ela pra onde quiser) | raquete 20% menor | 1 |
| Gêmea | 2ª raquete ao lado | vão de 28 px entre as duas | 1 |
| Elástico | estica até +40% quando você mexe | encolhe até -30% quando para | 1 |

### Bola

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Chumbo | bola 25% mais lenta | bola menor | 3 |
| Melancia | bola maior | cai em curva (gravidade) | 2 |
| Gêmeas | rebatida vale 2 | duas bolas em jogo (cada uma que cai tira vida) | 1 |
| Cometa | pontinhos mostram a trajetória | bola 20% mais rápida | 1 |
| Fantasma | ganha 1 vida | bola some no meio da mesa | 2 |
| Tiro | rebatida no centro vale 3 | ângulo de até 75° na borda | 1 |

### Controle

As que só existem porque o KIT está na mão.

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Espelho | 50% mais pontos | controle invertido | 1 |
| Mola | 40% menos inclinação até a borda | sem zona morta, tremor amplificado | 2 |
| Pena | movimento suavizado | ~100 ms de atraso | 1 |
| Prumo | ganha 1 escudo | o eixo vira frente/trás | 1 |
| Sacode | 3 cargas: chacoalhar dá cortada na bola que sobe (reta, rápida, +3 na parede) | a cada ~20 s a raquete escorrega pra um lado | 2 |
| Freio | 2 cargas: chacoalhar dá ~2 s de câmera lenta na bola que desce | bola 25% mais rápida | 2 |

### Percepção

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Apagão | ganha 1 vida | a tela apaga ~1 s a cada 15 rebatidas (8 com 2 cópias) | 2 |
| Neblina | 50% mais pontos | o topo da mesa fica coberto (35%; 50% com 2 cópias) | 2 |
| Mudo | 25% mais pontos | a bola não faz som | 1 |
| Metrônomo | bipe ~0,3 s antes da bola chegar | velocidade em ondas de ±20% | 1 |

### Parede

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Prensa | pontos em dobro | a parede desce 40 px | 3 |
| Portal | rebatida depois de passar num portal vale 3 | as laterais viram portais | 1 |
| Rebote | a parede devolve a bola reta | a cada 10 batidas, um ângulo maluco | 1 |

### Meta

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Faxina | tira o ônus mais antigo | tira o bônus mais antigo | 3 |
| Exorcismo | tira todos os ônus | fica com 1 vida | 2 |
| Vidro | ganha 2 vidas | cada bola perdida tira 2 | 1 |
| Fênix | revive uma vez com 1 vida | perde 25% no placar final | 1 |
| Ganância | 4 cartas por escolha | escolhas a cada 5 rebatidas | 1 |

Sacode e Freio dividem o chacoalhar. O Freio socorre a bola que desce; a
Sacode corta a que sobe. O KIT só dispara o chacoalhar acima de ~2,2 g, então
inclinar não aciona.

**Tijolos** (a parede vira tijolos que valem pontos) ficou pra v1.1: muda a
física da parede e custa objetos LVGL.

## Giroscópio

A API das Tools **não expõe o acelerômetro cru**. Há só o chacoalhar, o gesto
de inclinar da Testa (discreto) e o giroscópio integrado (`imu->gyro_*`, em
centigraus, runtime ≥ 0.4.0). O Quique usa o **roll** do giroscópio (o pitch
com Prumo):

- **Deriva:** o ângulo integrado deriva com o tempo. O centro "vaza" devagar
  em direção ao ângulo atual (constante de ~30 s). Isso corrige a deriva sem
  atrapalhar a mão que segura uma inclinação por 1 ou 2 s.
- **Sempre lido:** o giroscópio só integra quando é lido. Por isso a Tool lê a
  cada quadro enquanto a partida existe, inclusive na pausa e na escolha de
  carta; senão a rotação feita no meio tempo se perderia.
- **Sentido não validado no hardware:** o sinal do roll no aparelho real não
  foi conferido. Se a raquete fugir pro lado errado, use **DIREÇÃO:
  INVERTIDA** no AJUSTE.

## Som

Bipe curto na rebatida (mais agudo no centro com Tiro), na parede, nas
laterais e no portal (o Mudo cala todos os sons da bola), `kit_ui_miss` ao
perder bola, duas notas
subindo no escudo, `KIT_SFX_REVEAL` ao abrir as cartas, `KIT_SFX_VETO_HIT` na
cortada, `KIT_SFX_ONBOARD_DONE` no recorde e `KIT_SFX_ADEDONHA_STOP` no fim
sem recorde.

## Persistência

Sensibilidade (`qq_sens`), direção (`qq_dir`) e o top-5 (`qq_hs0`..`qq_hs4`,
só pontos, sem sigla). A partida não sobrevive a fechar/reabrir. A tela fica
acesa (`power->keep_awake`) da calibração ao fim.

## Build

```bash
cmake -B build -S . -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk
cmake --build build && ctest --test-dir build      # lógica (test_quique)
kit-cli build . --target xtensa                     # tool.so
```

A lógica pura fica em `src/quique_game.c`: inteiro puro, sem float nem
divisão de 64 bits, com física em subpixels (1/16 px) e passo de 16 ms. Os
testes (`test_quique.c`) cobrem a física, a cadência das escolhas, o acúmulo e
os limites, cada carta de vida e de limpeza, portal, escudo, chacoalhar, o
top-5 e um fuzz de 40 partidas com cartas aleatórias checando invariantes. O
ícone sai de `scripts/make_icon.py` (`icon.svg` é a fonte de verdade).

## A validar no hardware

- **Fluidez:** é o primeiro jogo do catálogo com animação contínua. O quadro é
  de 16 ms, e a Tool só mexe no LVGL quando algo muda (posição, tamanho,
  visibilidade).
- **Sinal do roll** (ver *Giroscópio*).
- **Leitura inclinada:** acima de ~30° a tela fica difícil de ler, e por isso
  a sensibilidade suave é o máximo.
- **Balanceamento:** os números (percentuais, tempos, cadência) são ponto de
  partida.

## Licença

GPL-3.0, como o resto do catálogo.
