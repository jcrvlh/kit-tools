<!-- Nova Tool ou atualização de uma existente. -->

## Tool

- **id:** `` (domínio reverso, == nome da pasta)
- **Nome:**
- **Versão / version_code:**
- [ ] Nova Tool  ·  [ ] Atualização

## O que faz

<!-- Telas, disparos (botão / toque / PWR / chacoalhar), persistência. -->

## Checklist

- [ ] `python scripts/validate_tool.py tools/<id>` passa
- [ ] Build nativo passa (`cmake` com `-DKIT_SDK_PATH=…`)
- [ ] `tool_destroy` libera todos os widgets, timers e alocações
- [ ] Só usa APIs declaradas em `permissions`
- [ ] `README.md` da Tool preenchido (o que faz, como testar, licença)
- [ ] Aderente à linguagem visual do KIT

## Permissões declaradas

<!-- Liste e justifique. `network` exige justificativa detalhada. -->

## Como foi testada

<!-- Hardware real? Simulador? Qual placa / versão de ESP-IDF? -->
