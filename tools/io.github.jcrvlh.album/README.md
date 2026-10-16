# Álbum

**Álbum** é um álbum de figurinhas de Wi-Fi para o KIT. Abrir a Tool é abrir o
pacote: o KIT olha as redes Wi-Fi em volta e cada rede nova, perto o bastante,
vira uma figurinha. A arte sai dos dados da própria rede, então a mesma rede dá
sempre a mesma figurinha.

O KIT só **olha** as redes: não conecta em nenhuma e não manda nada pra internet.
Funciona com o Wi-Fi do KIT desligado.

---

## Como Jogar

1. Abra o Álbum onde estiver. O KIT procura as redes em volta e volta a procurar
   a cada poucos segundos enquanto a Tool fica aberta.
2. **Inédita** é a rede que cola uma vaga nova do álbum. Ela vem num envelope
   fechado: arraste o dedo na linha tracejada pra rasgar e ver a figurinha.
3. **Repetida** é a rede nova que não cola vaga nenhuma. Ela entra direto em
   "Todas as redes", sem envelope.
4. Só pega rede perto (sinal de -75 dBm ou mais forte). Se aparecer "longe",
   ande uns passos com o KIT na mão.
5. Abrindo sempre no mesmo lugar, em 3 dias diferentes, o KIT pergunta que lugar
   é (Casa, Trabalho, Escola...), já sugerindo pelo horário.

### As páginas do álbum (54 vagas)

| Página | Vagas |
| --- | --- |
| **Canais** | uma por canal, de 1 a 13 (12 e 13 são raros) |
| **Segurança** | aberta, WEP, WPA2, WPA3 |
| **Horários** | madrugada, manhã, almoço, tarde, noite (hora da captura) |
| **Letras** | de A a Z, pela primeira letra do nome da rede (acento conta pela letra base: Á é A, Ç é C) |
| **Especiais** | oculta, nômade (hotspot de celular), bando (3 nós da mesma mesh), encostado (sinal de -35 dBm ou mais), fiel (vista em 7 dias diferentes), multidão (20+ redes num scan só) |

Fora as vagas, toda rede capturada entra em **Todas as redes**, sem limite de
página (até 384 redes no álbum).

### A arte

- **Cor de fundo = canal:** 1 a 4 vermelho, 5 a 8 amarelo, 9 a 13 azul.
- **Moldura = segurança:** aberta sem moldura, WPA/WPA2 uma, WPA3 duas,
  Enterprise uma grossa, WEP picotada como selo antigo.
- **Formas = tipo de rede:** oculta sai como fantasma translúcido sobre preto,
  hotspot de celular sai com as formas espalhadas, mesh sai em trio.
- **Brilhante** é a rara (oculta, celular, WEP, WPA3, canal 12 ou 13): tem fio
  claro na grade e um brilho correndo no pacote e na ficha.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus via `kit_ui.h` — shell AJUSTE ◄─ JOGO ─► COMO JOGA,
  abre no JOGO.
  - **AJUSTE**: mostrar ou esconder o nome da rede; zerar o álbum (toque duplo).
  - **JOGO**: lugar atual, redes em volta (ou quantas estão longe), número de
    inéditas pra abrir, botões ABRIR PACOTE e ÁLBUM.
  - **COMO JOGA**: regras.
  - Por cima, uma pilha de vistas: Pacote, Álbum, Página, Ficha, Lugares e a
    pergunta Lugar novo.
- **Scan**: `ctx->api->wifi` (runtime ≥ 0.18.0, permissão `"wifi_scan"`), a cada
  6 s com a Tool aberta.
- **Lógica pura** em `src/album.c` (testada por `test_album.c`): classificação
  inédita/repetida, vagas, lugares pela impressão digital das redes (≥ 50% das
  mesmas redes = mesmo lugar), gravação.
- **Gravação**: `album_a.bin` / `album_b.bin` no armazenamento interno da Tool
  (~21 KB), alternando a cada gravação; vale o mais novo com CRC íntegro. A
  versão 1 do arquivo (sem Letras) é migrada ao abrir: as redes já reveladas
  preenchem as letras.
- **Formas**: círculo e quadrado são `lv_obj`; o triângulo é máscara A8 em 6
  tamanhos fixos (`scripts/make_shapes.py` gera `src/shapes.h`).

## Compilar

```bash
python3 scripts/make_shapes.py      # só se mexer nos tamanhos
python3 scripts/make_icon.py        # só se mexer no ícone
cmake -B build -S . -DKIT_SDK_PATH=~/Projetos/kit/tools-sdk && cmake --build build && (cd build && ctest)
kit-cli build . --target xtensa     # tool.so
```
