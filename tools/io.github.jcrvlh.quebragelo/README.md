# Quebra-Gelo

A Tool **Quebra-Gelo** sorteia perguntas leves, divertidas e criativas de um
baralho com 95 opções para animar conversas, encontros de amigos, dinâmicas de
equipe ou jantares.

Fiel à filosofia do KIT: sorteio rápido, direto e visualmente marcante, sem
fricção ou configurações desnecessárias.

---

## Como Usar

1. Toque no botão **SORTEAR**, em qualquer lugar do palco ou chacoalhe o KIT.
2. Uma pergunta é sorteada com efeito de catraca e revelada em destaque.
3. Leia a pergunta em voz alta, responda e passe o KIT para a próxima pessoa!

---

## Estrutura da Tool

- **UI**: Brutalist Bauhaus (`kit_theme.h` e `kit_fonts.h`). Página única com titlebar, pergunta central em `kit_mono_26` e botão de ação em azul (`#2C3CC4`).
- **Animação**: Sorteio em ease-out com SFX de catraca (`KIT_SFX_BOTTLE_SPIN`) sincronizado.
- **Entradas**: Toque na tela, botão `SORTEAR` ou gesto de chacoalhar (IMU).
