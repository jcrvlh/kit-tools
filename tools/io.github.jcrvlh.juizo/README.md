# Juízo

**Juízo** é a Bola 8 do KIT, com identidade brasileira: faça uma pergunta,
toque no orbe e receba um veredito curto — seco, espontâneo, levemente
debochado. Sem chatbot e sem interpretar a pergunta: a resposta é sempre
sorteada de um banco fixo de 23 frases.

Fiel à filosofia do KIT: objeto → ritual → veredito, sem formulário nem
explicação de mais.

---

## Como Usar

1. Toque no orbe, no botão **PERGUNTAR** ou chacoalhe o KIT.
2. O orbe balança enquanto o Juízo "pensa" — a resposta pisca rápido antes
   de travar.
3. O veredito aparece em destaque. Pergunte de novo quando quiser.

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` e `kit_fonts.h`). Página única
  com titlebar, orbe vermelho (`#C6472F`) e resposta central em
  `kit_sans_28`.
- **Animação**: flicker em ease-out (16 quadros) com o orbe balançando por
  `translate_x` — sem float, só inteiro.
- **Entradas**: toque no palco, botão `PERGUNTAR` ou gesto de chacoalhar
  (IMU).
