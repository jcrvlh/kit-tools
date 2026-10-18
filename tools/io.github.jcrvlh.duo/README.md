# Duo — `io.github.jcrvlh.duo`

Jogo de dupla passando o KIT de mão em mão. Os dois respondem as mesmas
perguntas, um de cada vez e sem o outro ver, e no fim veem lado a lado o
quanto bateu.

- **Me conhece:** um responde sobre si e o outro tenta adivinhar o que ele
  respondeu. No fim dá pra trocar os papéis.
- **Combina:** os dois respondem sobre si.

## Páginas

```
AJUSTE  ◄──►  JOGO  ◄──►  COMO JOGA
```

Abre sempre no **JOGO**.

- **AJUSTE:** modo (Me conhece / Combina), perguntas (3, 5, 7 ou 10; padrão
  5) e tema (Geral, Amigos, Casal, Polêmica).
- **JOGO:** o número de perguntas, o tema, o modo e **COMEÇAR**.
- **COMO JOGA:** as regras.

## A rodada

Durante a rodada as páginas ficam travadas no JOGO. A seta ← sai da Tool,
como em todas as outras.

1. **Vez do P1:** a tela fica azul. Quem pega o KIT segura o dedo na tela
   até a barra encher (0,9 s) pra começar. Um toque solto não pula a tela.
2. **Perguntas:** com 2 opções a tela se divide em duas metades; com 4 vira
   uma grade 2x2. As opções entram uma por uma, num fade curto, e só aceitam
   toque depois que a última apareceu. A opção escolhida acende e a próxima pergunta entra.
3. **Vez do P2:** a tela azul de novo, e as mesmas perguntas, em outra ordem
   e com as opções em outras posições: quem acabou de responder não entrega a
   resposta pelo lugar onde tocou. A revelação segue a ordem do P1.
4. **Os dois juntos:** segurar pra revelar.
5. **Revelação, uma por uma:**
   - **Bateu:** tela verde, as duas respostas fundidas num bloco só e
     **BATEU** em letra grande.
   - **Não bateu:** tela preta, dois blocos desencontrados (um sobe, o outro
     desce), cada um com a sua resposta.
   - Embaixo, um quadrado por pergunta vai marcando o placar.
   - Toque em qualquer lugar pra seguir.
6. **Resultado:** o % sobe de 0 até o valor, centralizado, e a cada 10% toca
   uma nota de uma escala subindo. Depois o número sobe pro lugar e aparecem a frase,
   **JOGAR DE NOVO** e, no Me conhece, **TROCAR PAPÉIS**. Aqui as páginas
   destravam, dá pra mudar o AJUSTE antes de jogar de novo.

O % é `acertos × 100 / perguntas`, arredondado pra baixo (6 de 7 = 85%).

| Me conhece | Combina |
| --- | --- |
| 100: Leu a mente | 90+: Almas gêmeas |
| 80+: Conhece de cor | 70+: Muito parecidos |
| 60+: Conhece bem | 50+: Combinam |
| 40+: Mais ou menos | 30+: Opostos se atraem? |
| 20+: Precisam conversar | abaixo: Água e vinho |
| abaixo: Quase estranhos | |

## As perguntas

369 perguntas em `data/perguntas.tsv` (Geral 91, Amigos 92, Casal 92,
Polêmica 94): "Quem...?" (resposta P1 ou P2), sim ou não, 2 opções e 4
opções. `scripts/make_bank.py` gera `src/duo_bank.h` e reprova o banco se:

- uma pergunta não tem 2 ou 4 opções, ou uma opção de 4 passa de 10 letras;
- aparece travessão, aspas curvas ou reticências de 1 caractere (sem glifo
  na fonte do KIT);
- uma pergunta se repete dentro do tema, ou o tema passa de 96 perguntas.

A 4ª coluna `conhece` marca fatos que os dois sabem ("Como vocês se
conheceram?"): essas só entram no Me conhece, porque no Combina sempre
bateriam.

**Inéditas primeiro.** Cada tema guarda 1 bit por pergunta já vista (3
inteiros por tema na NVS). A rodada sorteia entre as inéditas; quando
acabam, começa um ciclo novo e as já vistas voltam.

## Persistência

- `duo_modo`, `duo_nq`, `duo_tema`: os ajustes.
- `duo_v<tema><0..2>`: o bitmap de inéditas de cada tema.

A rodada em andamento **não** sobrevive a fechar e reabrir a Tool.

## Build

```bash
python3 scripts/make_bank.py          # depois de editar data/perguntas.tsv
python3 scripts/make_icon.py          # depois de editar icon.svg
cmake -B build -S . -DKIT_SDK_PATH=~/Projetos/kit/tools-sdk && cmake --build build
./build/test_duo                      # sorteio, filtro do Combina, %, frases, banco
kit-cli build . --target xtensa       # tool.so
```

Requer runtime **0.15.0**. Inteiro puro, sem `float`.

**Som.** O firmware desliga o amplificador depois de 3 s sem som e religar
estala e pode engolir o 1º toque — no Duo quase todo toque vem depois de uns
segundos lendo a pergunta. Por isso, durante a rodada, a Tool manda a cada 2 s
um "bipe" de frequência 0 (só zeros no I2S), que mantém o amplificador ligado.
O toque da resposta é o `KIT_SFX_CLICK`, igual pra todas as opções (um tom
por opção entregaria a resposta pro outro jogador). O resultado sobe numa
escala maior, uma nota de 95 ms a cada 10% (C5 a E6), e fecha num C6 curto: o
mesmo desenho da revelação do Sortear Times.
