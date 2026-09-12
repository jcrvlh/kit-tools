# Juízo

**Juízo** é o veredito seco do KIT: faça uma pergunta, toque na pedra e
receba uma resposta curta — espontânea, levemente debochada. Sem chatbot e
sem interpretar a pergunta: a resposta é sempre sorteada de um banco fixo
de 23 frases.

O objeto é uma pedra **quadrada** (não redonda — de propósito, pra não
copiar a Bola 8), com uma janela circular no centro onde o veredito
aparece. Fiel à filosofia do KIT: objeto → ritual → veredito, sem
formulário nem explicação de mais.

---

## Como Usar

1. Toque na pedra, no botão **PERGUNTAR** ou chacoalhe o KIT.
2. A pedra dá 3 saltos decrescentes (um thud a cada pouso), segura um
   instante de suspense e a tela pisca antes do veredito travar na janela.
3. Pergunte de novo quando quiser.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` e `kit_fonts.h`). Página única
  com titlebar, pedra quadrada arredondada com anel vermelho (`#C6472F`)
  e janela circular escura onde o veredito aparece, em `kit_sans_22`.
- **Animação**: 3 saltos decrescentes (`translate_y`, com thud por
  `beep()` a cada pouso), suspense e um flash de tela cheia que desbota em
  degraus, revelando a resposta já travada por baixo — tudo por
  `lv_timer` + `beep()` de duração explícita, sem `kit_sfx_t` de duração
  fixa tocando por cima da animação.
- **Entradas**: toque no palco, botão `PERGUNTAR` ou gesto de chacoalhar
  (IMU).
