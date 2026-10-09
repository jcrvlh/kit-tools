# Quique — `io.github.jcrvlh.quique`

Ping-pong **sozinho** contra a parede. A raquete segue o **dedo** (padrão)
ou a **inclinação do KIT** (opcional no AJUSTE). A cada
10 rebatidas o jogo pausa e abre **3 cartas**: cada uma traz um **bônus e um
ônus juntos**, e os efeitos **acumulam** até o fim. Com 60 pontos, sua raquete
é uma gambiarra de escolhas suas. Três vidas; acabou, o placar entra (ou não)
no top-5.

## A mesa

A tela de jogo é só a mesa: a parede fica quase na borda de cima, e o
**botão de pausa** (só contorno) fica no canto de cima, dentro da mesa.

- **Toque:** embaixo da mesa há uma **faixa do dedo** (72 px, com uma pegada
  no centro). A raquete fica logo acima dela, e as **vidas** (pontinhos)
  ficam dentro dela. A bola que passa da raquete some na faixa.
- **Inclinação:** sem faixa. A raquete fica embaixo e as vidas no canto de
  cima, espelhando a pausa.

O resto se mostra dentro da mesa:

- **Placar:** número grande em marca d'água no centro da mesa, atrás da bola.
- **Cargas** de SACODE (furinhos escuros) e FREIO (claros): pontinhos na
  própria raquete.
- **Escudo:** linha azul embaixo da raquete. **Portais:** laterais azuis.
  **Neblina, Prensa, Gêmea, Gêmeas, Cometa:** aparecem na própria mesa.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA  ◄──►  CARTAS
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Controle:** toque (padrão) ou inclinação.
  - **Sensibilidade:** suave, normal ou viva. No toque, a raquete passa do
    dedo 1×, 1,15× ou 1,4× a partir do meio; na inclinação, 35°, 25° ou 15°
    levam até a borda.
  - **Direção (só inclinação):** normal ou invertida (ver *Giroscópio*).
- **JOGO:** a mesa (protagonista), o recorde, o top-5 e **COMEÇAR**.
- **COMO JOGA:** as regras.
- **CARTAS:** o banco inteiro, com bônus e ônus de cada carta.

## A partida

1. **Começo:** no toque, a partida começa direto ("DEDO NA FAIXA"). Na
   inclinação, "SEGURE O KIT RETO" por ~1 s; o giroscópio zera e essa
   posição vira o centro.
2. **Controle:**
   - **Toque:** posição **absoluta** na faixa do dedo. A raquete fica em
     cima do dedo, ampliada a partir do centro pela sensibilidade (1× /
     1,15× / 1,4×), pra chegar na borda antes de o dedo chegar no canto
     arredondado. O toque que **começa** na faixa (ou até 16 px acima dela)
     controla até o dedo sair, mesmo se subir pra mesa. Toque que começa na
     mesa não mexe a raquete. Sem dedo, a raquete fica onde está.
   - **Inclinação:** o ângulo define a **posição** da raquete (não a
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
7. **Pausa:** o botão no canto de cima. A pausa mostra a **montagem**: cada
   carta ativa, com o + e o - que ainda valem (a Faxina pode ter tirado um
   lado). **CONTINUAR** dá 0,8 s ("VAI!") antes de soltar a bola;
   **ENCERRAR** vai pro fim com o placar valendo.
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
- As cartas ativas aparecem onde importam: na tela de escolha (uma linha com
  a montagem atual), na pausa (a lista completa) e no fim. Na volta de uma
  escolha, o nome da carta pega fica apagado acima da raquete enquanto a bola
  espera. Isso lembra as cartas que não se veem na mesa (Espelho, Mola,
  Prumo...).

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

Valem nos dois controles; Sacode e Freio usam o chacoalhar, que é outro
sensor.

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Espelho | 50% mais pontos | controle invertido | 1 |
| Mola | a raquete corre mais (toque: ganho ×1,25; inclinação: 40% menos ângulo até a borda) | tremor amplificado (sem zona morta; cada movimento dá um tranco além do ponto) | 2 |
| Pena | movimento suavizado | ~100 ms de atraso | 1 |
| Prumo | ganha 1 escudo | o eixo do controle troca (toque: a ALTURA do dedo, em qualquer lugar da tela, vira o lado — topo = esquerda; inclinação: frente/trás) | 1 |
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

## Toque

O firmware entrega à Tool uma leitura crua (`KIT_INPUT_TOUCH_DOWN`, com x/y)
por amostra do sensor (~30 ms) enquanto o dedo está na tela, e **nada** ao
soltar. A Tool guarda a última posição e entrega ao jogo a cada quadro. A
soltura é detectada pela falta de leitura (~100 ms). Um salto de mais de
90 px entre duas leituras conta como dedo novo (o CST820 lê um ponto só e
"pula" quando entra um segundo dedo), e o dedo novo só controla se começou na
faixa. Com Prumo, vale qualquer lugar, menos o botão de pausa.

## Giroscópio

A API das Tools **não expõe o acelerômetro cru**. Há só o chacoalhar, o gesto
de inclinar da Testa (discreto) e o giroscópio integrado (`imu->gyro_*`, em
centigraus, runtime ≥ 0.4.0).

- **Inclinação lateral = yaw + roll.** Inclinar pro lado (como um volante) é
  girar em torno da linha de visão. Com o KIT a θ da vertical, esse giro cai
  em **yaw · cos θ** (eixo normal à tela) + **roll · sin θ** (eixo da altura
  da tela), com o mesmo sinal (eixos do QMI8658 conforme a calibração do
  firmware: gx = altura, gy = largura, gz = normal). A soma funciona com o KIT
  em pé, deitado ou no meio, com ganho entre 1× e 1,41×. A v1.0 lia só o roll
  e a raquete quase não andava com o KIT em pé.
- **Prumo** troca pro pitch (inclinar pra frente/trás).
- **Deriva:** o ângulo integrado deriva com o tempo. O centro "vaza" devagar
  em direção ao ângulo atual (constante de ~30 s). Isso corrige a deriva sem
  atrapalhar a mão que segura uma inclinação por 1 ou 2 s.
- **Sempre lido:** o giroscópio só integra quando é lido. Por isso a Tool lê a
  cada quadro enquanto a partida existe, inclusive na pausa e na escolha de
  carta; senão a rotação feita no meio tempo se perderia.
- **Sentido:** se a raquete fugir pro lado errado, use **DIREÇÃO: INVERTIDA**
  no AJUSTE. Efeito colateral da soma: com o KIT em pé, girar o corpo (em
  torno da vertical do mundo) também mexe a raquete um pouco.

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
