# Quadrado

**Quadrado** é uma homenagem ao primeiro aplicativo Android do autor (2017,
"Bola/Quadrado"): a tela mostra um quadrado e uma bola, um de cada lado.
Toque em **COMEÇAR**, depois toque **sempre no quadrado** — cada acerto vale
5 pontos e sorteia se o lado troca ou não (50/50, ritmo constante). Tocar na
bola encerra a rodada na hora; o tempo escolhido no AJUSTE também encerra ao
zerar.

A área de toque é a metade inteira da tela, não a forma — o erro só pode ser
de decisão, nunca de mira. As duas formas são sempre azuis (a cor base da
Tool): só a silhueta — quina viva contra círculo — identifica qual é qual.

---

## Como Jogar

1. Escolha o **tempo da partida** no AJUSTE (15/30/60 s) e, se quiser, o
   **Modo Inverte**.
2. Na página JOGO, toque em **COMEÇAR** — as formas só aparecem depois disso.
3. Toque sempre no **QUADRADO**. Cada acerto soma 5 pontos e re-sorteia se o
   lado troca ou fica igual.
4. Tocar na **BOLA**, ou o tempo zerar, acaba a rodada.
5. Entrou no top-5 (um pra cada modo)? Arraste pra cima ou pra baixo em cada
   caixa pra rolar as letras — como uma roleta — ou toque pra avançar uma. A
   sigla vem pré-preenchida com a última usada, com um botão **REDEFINIR**
   pra zerar. Toque em **SALVAR**.

### Modo Inverte (AJUSTE, desligado por padrão)

Com o Modo Inverte ligado, o alvo também pode virar a bola no meio da
rodada — junto com o sorteio de lado. Um ícone acima das formas mostra qual
vale agora (quadrado ou bola), sem texto. Tem highscore próprio, separado
do modo normal — os dois jogos pedem reflexos bem diferentes.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus, montada com a galeria de componentes do SDK
  (`kit_ui.h`, doc em `jcrvlh/kit` → `tools-sdk/docs/ui_gallery.md`) —
  `kit_ui_shell` (titlebar + tileview), `kit_ui_chips`, `kit_ui_help_page`,
  `kit_ui_action` e `kit_ui_sigla` (o seletor de sigla nasceu nesta Tool e
  virou componente). Só a tabela de HIGHSCORES continua feita à mão.
  - Página 0: **AJUSTE** — chips de TEMPO (15/30/60 s) e MODO INVERTE
    (DESLIGADO/LIGADO), rola se não couber.
  - Página 1: **JOGO** — quatro estados no mesmo tile (o swipe entre as
    páginas fica **travado** enquanto a rodada não termina):
    - parado: recorde do modo selecionado + "TOQUE EM COMEÇAR", botão
      COMEÇAR fixo no rodapé;
    - jogando: placar, tempo restante e recorde no topo; as duas metades
      com as formas (linha fina só de divisa, sem moldura), ícone do alvo
      atual acima só com Modo Inverte ligado;
    - fim de rodada: overlay vermelho cheio **TEMPO** (tempo zerou) ou
      **PERDEU** (tocou na bola) + "TOQUE PARA CONTINUAR" — uma barreira
      antes do resultado pra não apertar SALVAR no susto;
    - resultado: pontuação grande e, se entrou no top-5, o **seletor de
      sigla** (`kit_ui_sigla`: toque avança a letra, arraste pra cima/baixo
      gira como roleta, com tolerância pra não trocar a letra ao soltar o
      dedo; botão REDEFINIR); senão, `RECORDE: XXX NNN`. Mesmo botão fixo do
      rodapé, agora rotulado SALVAR/JOGAR DE NOVO. Rola se precisar.
  - Página 2: **COMO JOGA** (regra resumida + a homenagem), rola.
  - Página 3: **HIGHSCORES** — top-5 do modo normal e top-5 do Modo Inverte,
    em seções separadas.
- **Persistência**: dois top-5 (20 chaves i32/str, um por modo), tempo e
  Modo Inverte escolhidos, e a última sigla digitada — tudo sobrevive a
  fechar/reabrir e a reinicializações (`storage->set_i32`/`get_i32`,
  `set_str`/`get_str`).
- **Áudio**: `KIT_SFX_CLICK` — cliquezinho sutil — no acerto e nos demais
  toques de UI (letras, chips, redefinir), duas notas curtas descendo
  (A4→D4) no erro, `KIT_SFX_TIMER_DONE` quando o tempo zera,
  `KIT_SFX_CONFIRM` ao salvar um highscore novo.
- **Entradas**: toque — as duas metades da tela como alvo (nunca a forma em
  si) e os botões padrão. O arraste na roleta da sigla vai por
  `kit_input_api_t.register_callback` (o único callback de toque bruto que
  uma Tool pode registrar), repassado pra `kit_ui_sigla_feed_touch()`.
