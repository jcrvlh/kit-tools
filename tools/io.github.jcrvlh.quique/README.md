# Quique — `io.github.jcrvlh.quique`

Ping-pong **sozinho** contra a parede, controlado **inclinando o KIT**. Sem
calibrar: com o KIT nivelado, a raquete fica no meio. A cada 10 rebatidas o
jogo pausa e abre **3 cartas**: cada uma traz um **bônus e um
ônus juntos**, e os efeitos **acumulam** até o fim. Com 60 pontos, sua raquete
é uma gambiarra de escolhas suas. Três vidas; acabou, o placar entra (ou não)
no top-5.

## A mesa

A tela de jogo é só a mesa: a parede fica quase na borda de cima, o
**botão de pausa** (só contorno) fica no canto de cima, dentro da mesa, e as
**vidas** (pontinhos) ficam no outro canto, espelhando a pausa.

O resto se mostra dentro da mesa:

- **Placar:** número grande em marca d'água no centro da mesa, atrás da bola.
- **Cargas** de SACODE (furinhos escuros) e FREIO (claros): pontinhos na
  própria raquete.
- **Escudo:** linha azul embaixo da raquete. **Portais:** laterais azuis.
  **Neblina, Prensa, Gêmea, Gêmeas, Cometa:** aparecem na própria mesa.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Sensibilidade:** suave, normal ou viva: 35°, 25° ou 15° de inclinação
    levam a raquete até a borda.
- **JOGO:** a mesa (protagonista), o recorde, o top-5 e **COMEÇAR**.
- **COMO JOGA:** as regras.

## A partida

1. **Começo:** a partida começa direto ("INCLINE O KIT"), sem calibrar.
2. **Controle:** o ângulo da borda da tela contra a gravidade define a
   **posição** da raquete (não a velocidade). Ela corre pro lado que desce,
   como uma bolinha. Zona morta pequena (0,8°) pra mão não tremer a raquete.
3. **Saque:** a bola fica ~0,7 s na raquete e sai pra cima, com um ângulo
   sorteado de até 15°.
4. **Rebatida:** vale 1 ponto × o multiplicador. O ângulo de volta depende de
   onde a bola bateu (centro = reto, borda = até 60°). A parede de cima devolve
   sempre. A bola acelera 6% a cada 10 rebatidas (teto de +60%).
5. **Vida:** a bola passou da raquete, perde 1 de 3 e volta pro saque.
6. **Escolha:** a cada 10 rebatidas, a mesa some e aparecem 3 cartas abertas
   (4 com a Ganância) num **carrossel**: uma carta grande no meio, as
   vizinhas espiando apagadas. **Inclinar escolhe**, com o mesmo gesto e a
   mesma sensibilidade da raquete: as cartas dividem a faixa de inclinação, e
   nivelado é a do meio. Perto da fronteira entre duas cartas a destacada só
   troca depois de passar 1,5° (histerese, pra não piscar), e cada troca toca
   um "tic" (`KIT_SFX_TAP`). O carrossel abre já na carta pra qual a mão
   aponta e desliza desacelerando. **Tocar em qualquer lugar** pega a
   destacada. Os toques são ignorados nos primeiros 0,7 s (o "TOQUE PRA
   PEGAR" acende quando libera). Não dá pra pular. Depois da escolha, a bola
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

Mexem no próprio controle. Sacode e Freio usam o chacoalhar, que o KIT só
dispara acima de ~2,2 g, então inclinar não aciona.

| Carta | + | - | Máx. |
| :--- | :--- | :--- | :---: |
| Espelho | 50% mais pontos | controle invertido | 1 |
| Mola | a raquete corre mais (40% menos ângulo até a borda) | tremor amplificado (sem zona morta; cada movimento dá um tranco além do ponto) | 2 |
| Pena | movimento suavizado | ~100 ms de atraso | 1 |
| Prumo | ganha 1 escudo | o eixo do controle troca: inclinar pra frente/trás (tombar pra trás = direita, pra frente = esquerda) | 1 |
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
Sacode corta a que sobe.

**Tijolos** (a parede vira tijolos que valem pontos) ficou pra v1.1: muda a
física da parede e custa objetos LVGL.

## Acelerômetro

O controle lê o vetor cru do acelerômetro (`imu->accel_poll`, runtime ≥
**0.15.0**), em mili-g, nos eixos da tela, já com a rotação do Modo canhoto
aplicada. Os ângulos saem de `qq_tilt_from_mg`, em inteiro (atan2 por
aproximação, erro < 0,3°; raiz quadrada inteira):

- **Lateral** = ângulo do eixo x acima do horizontal: `atan2(x, √(y²+z²))`,
  o mesmo que o `accel_tilt` do firmware dá. Borda direita mais alta = a
  raquete vai pra esquerda (o lado que desce). Sem calibrar e sem deriva:
  nivelado = centro, em qualquer pegada.
- **Frente/trás (Prumo)** = giro em torno do eixo x da tela, **assinado**:
  `atan2(z, −y)`. Em pé = 0, tombando pra trás > 0, pra frente < 0, deitado =
  90°. O ângulo do eixo y do `accel_tilt` não serve aqui porque dobra com o
  KIT em pé: pra frente e pra trás davam o mesmo valor, e com Prumo a raquete
  só ia pra um lado (bug da primeira versão com acelerômetro). Como
  frente/trás não tem "nível" natural, vale relativo ao ângulo de quando a
  carta entrou, com um vazamento lento (~30 s) pra mão que muda de pegada.
- **Tranco da mão:** o acelerômetro também mede a aceleração do movimento. A
  leitura passa por uma média móvel curta (1/3 por quadro, ~50 ms) antes de
  virar posição.
- **Histórico:** antes do 0.15.0 a API não expunha o acelerômetro. O Quique
  passou por giroscópio (relativo: precisava zerar parado e derivava) e por
  toque (faixa do dedo). As duas versões estão no histórico do git.

## Som

O alto-falante pequeno do KIT distorce nos graves: abaixo de ~600 Hz um bipe
curto "estoura" (`api_reference`, runtime 0.15.0). Todo som do Quique fica
acima disso:

- **Raquete:** `KIT_SFX_TAP`, a nota "pinçada" do firmware (D6, ataque de
  3 ms, decaimento exponencial, volume baixo). Com Tiro, um brilho agudo
  (1568 Hz) na rebatida de centro.
- **Parede / lateral / portal:** bipes de 880 / 784 / 1047 Hz com ≤ 14 ms,
  que o firmware toca a ~34% como "tique de textura". O Mudo cala esses e o
  da raquete.
- **Bola perdida:** duas notas descendo, 880 → 659 Hz. Fica fora do
  `kit_ui_miss` (440 → 294 Hz), que estoura.
- **Outros:** duas notas subindo no escudo, `KIT_SFX_REVEAL` ao abrir as
  cartas, `KIT_SFX_TAP` a cada troca de carta no carrossel,
  `KIT_SFX_VETO_HIT` na cortada, `KIT_SFX_ONBOARD_DONE` no recorde e
  `KIT_SFX_ADEDONHA_STOP` no fim sem recorde.

## Persistência

Sensibilidade (`qq_sens`) e o top-5 (`qq_hs0`..`qq_hs4`, só pontos, sem
sigla). A partida não sobrevive a fechar/reabrir. A tela fica acesa
(`power->keep_awake`) do começo ao fim, o que também mantém o acelerômetro
ligado.

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

## Testes no hardware

Jogado no KIT com firmware 0.15.0: controle por inclinação, escolha de carta
inclinando, sons e Prumo. Pontos de ajuste fino, se precisar:

- **Tranco da mão:** se a raquete "pular" em movimentos rápidos, alongar a
  média móvel (`ACC_SMOOTH` em `src/main.c`).
- **Balanceamento:** percentuais, tempos e cadência das cartas estão em
  `src/quique_game.c` (topo do arquivo e `QQ_CARDS`).

## Licença

GPL-3.0, como o resto do catálogo.
