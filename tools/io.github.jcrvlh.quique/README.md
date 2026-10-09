# Quique — `io.github.jcrvlh.quique`

> **Status: em design.** Este README é a especificação do jogo; código,
> `manifest.json`, `CMakeLists.txt` e ícone ainda não existem. Os números
> (percentuais, tempos, limites) são ponto de partida — o balanceamento sai
> jogando.

Ping-pong **solo** contra a parede, controlado **inclinando o KIT**. A cada
10 pontos o jogo pausa e oferece **3 cartas**: cada uma traz um **bônus e um
ônus juntos**. Os efeitos **acumulam** — com 60 pontos a sua raquete é uma
gambiarra de escolhas suas. Três vidas; acabou, o placar entra (ou não) no
top-5.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE**
  - **Sensibilidade:** suave (35° até a borda), normal (25°) ou viva (15°).
  - Durante a partida os ajustes travam e aparece **ENCERRAR PARTIDA** (pede
    um segundo toque).
- **JOGO:** o top-5 salvo e **COMEÇAR**.
- **COMO JOGA:** as regras e o banco de cartas.

## A partida

1. **Calibração:** a posição em que o KIT está ao tocar **COMEÇAR** vira o
   centro. Ninguém joga com o aparelho perfeitamente na horizontal.
2. **Controle:** a inclinação lateral define a **posição** da raquete (não a
   velocidade) — inclinar até o limite da sensibilidade leva à borda. Zona
   morta pequena e filtro leve pra mão não tremer a raquete.
3. **Saque:** a bola sai da raquete pra cima depois de um bipe.
4. **Ponto:** cada rebatida vale 1 (vezes o multiplicador ativo). A parede de
   cima devolve sempre; o ângulo de volta depende de onde a bola bateu na
   raquete (centro = reto, borda = aberto).
5. **Vida:** a bola passou da raquete, perde 1 de 3. A bola volta pro saque.
6. **Escolha:** a cada 10 pontos, pausa e aparecem 3 cartas sorteadas, abertas.
   Toca numa pra escolher; não dá pra pular.
7. **Fim:** sem vidas, mostra o placar, a "montagem" final (as cartas
   empilhadas) e se entrou no top-5. **DE NOVO** / **SAIR**.

A velocidade da bola sobe um pouco a cada 10 rebatidas, com ou sem cartas.

## Acúmulo

- **Carta repetida intensifica** o efeito (2× Tábua = raquete ainda maior).
- **Todo efeito tem teto e piso:** tamanho da raquete, tamanho e velocidade da
  bola, multiplicador.
- **Multiplicadores somam, não multiplicam:** ×2 + ×2 = ×3. Mantém o top-5
  comparável.
- **Cartas de limpeza** (Faxina, Exorcismo) existem pra evitar a espiral da
  morte de ônus empilhados.
- Os efeitos ativos ficam como **chips** na borda da tela.

## Banco de cartas

Nome com no máximo 10 letras. ✚ = bônus, ━ = ônus.

### Raquete

| Carta | ✚ | ━ |
| :--- | :--- | :--- |
| Tábua | raquete +40% | bola +15% de velocidade |
| Agulha | pontos ×2 | raquete −35% |
| Ímã | a bola gruda; você relança mirando pela inclinação | raquete −20% |
| Gêmea | 2ª raquete ao lado | um vão entre as duas, por onde a bola passa |
| Elástico | a raquete estica conforme a velocidade da inclinação | parada, encolhe |

### Bola

| Carta | ✚ | ━ |
| :--- | :--- | :--- |
| Chumbo | bola −25% de velocidade | bola menor |
| Melancia | bola maior | cai em curva (gravidade) |
| Gêmeas | toda rebatida vale 2 | duas bolas em jogo |
| Cometa | rastro que mostra a trajetória | bola +20% |
| Fantasma | +1 vida | a bola some no meio da tela |
| Tiro | rebatida no centro da raquete vale 3 | ângulo extremo na borda |

### Controle

As que só existem porque o KIT está na mão.

| Carta | ✚ | ━ |
| :--- | :--- | :--- |
| Espelho | pontos +50% | controle invertido |
| Mola | 15° já leva à borda | tremor amplificado |
| Pena | movimento suave, sem tremor | a raquete responde com atraso |
| Prumo | escudo +1 | o eixo troca: inclinar pra frente/trás |
| Sacode | chacoalhar dá cortada: bola reta, vale 3 (3 cargas) | a cada 20 s a raquete escorrega pra um lado |
| Freio | chacoalhar dá câmera lenta por 2 s (2 cargas) | bola +25% |

### Percepção

| Carta | ✚ | ━ |
| :--- | :--- | :--- |
| Apagão | +1 vida | a tela apaga 1 s a cada 15 rebatidas |
| Neblina | pontos +50% | o topo da tela fica coberto |
| Mudo | pontos +25% | a bola não faz som |
| Metrônomo | bipe avisa quando a bola vai chegar | a velocidade oscila em ondas |

### Parede

| Carta | ✚ | ━ |
| :--- | :--- | :--- |
| Tijolos | a parede vira tijolos que valem pontos | tijolo quebrado devolve a bola mais rápida |
| Prensa | pontos ×2 | a parede desce: a mesa encolhe |
| Portal | laterais viram portais, a bola atravessa | trajetória imprevisível |
| Rebote | a parede devolve sempre reta | a cada 10 rebatidas, um ângulo maluco |

### Meta

| Carta | ✚ | ━ |
| :--- | :--- | :--- |
| Faxina | remove o ônus mais antigo | remove também o bônus mais antigo |
| Exorcismo | remove todos os ônus | fica com 1 vida |
| Vidro | +2 vidas | cada bola perdida tira 2 |
| Fênix | revive uma vez com 1 vida | pontuação final −25% |
| Ganância | a próxima escolha mostra 4 cartas | escolhas passam a vir a cada 5 pontos |

**Escudo:** barreira na borda de baixo que salva uma bola e some.

## Escopo

- **v1.0:** física da bola, controle por inclinação, vidas, top-5 e as cartas
  de Raquete, Bola, Controle, Percepção e Meta.
- **v1.1:** Tijolos e Portal (mudam a física da parede/laterais) e o resto do
  grupo Parede.

## Riscos a validar antes das cartas

1. **Fluidez:** primeiro jogo do catálogo com animação contínua. Prova de
   conceito só com bola + raquete pra medir se o LVGL segura o alvo de 60 FPS
   na AMOLED.
2. **Leitura inclinada:** a tela inclina junto com o controle; acima de ~30° a
   leitura piora. Isso limita a sensibilidade "suave".
3. **Chacoalhar vs inclinar:** Sacode e Freio usam chacoalhada; ela não pode
   mexer a raquete nem disparar sem querer.

## Persistência

Ajustes (sensibilidade) e o top-5. A partida não sobrevive a fechar/reabrir.
A tela fica acesa (`power->keep_awake`) durante a partida.

## Permissões previstas

`display`, `input`, `imu`, `random`, `storage`, `audio`, `power`.

## Licença

GPL-3.0, como o resto do catálogo.
