# Enviar uma Tool para o catálogo

## Requisitos da Tool

- **Desacoplada:** só usa `kit_tool_api.h`; nada de registradores, I2C/SPI ou GPIO.
- **LVGL v9** para a interface, mirando 60 FPS na tela AMOLED.
- **Libera tudo em `tool_destroy`** — widgets, timers, memória, arquivos.
- Entregue como pasta em `tools/<id>/`, empacotável em `.kit` pela CI.
- Aderente à linguagem visual do KIT
  ([design-language](https://github.com/jcrvlh/kit/blob/main/docs/design/design-language.md)).

## Estrutura de `tools/<id>/`

| Arquivo | Obrigatório | Observação |
| :--- | :---: | :--- |
| `manifest.json` | ✅ | id == nome da pasta; ver [manifest spec](https://github.com/jcrvlh/kit/blob/main/docs/tools/manifest.md) |
| `CMakeLists.txt` | ✅ | build nativo via `KIT_SDK_PATH` (veja o exemplo) |
| `src/*.c` | ✅ | ponto de entrada `tool_init` / `tool_destroy` |
| `icon.bin` | ✅ | ícone LVGL para o Launcher |
| `README.md` | ✅ | o que faz, como testar, licença |
| `assets/` | — | recursos estáticos opcionais |

## Fluxo

1. `git checkout -b tool/<id>` e adicione `tools/<id>/`.
2. Localmente:
   ```bash
   pip install <kit>/tools-sdk/cli jsonschema
   python scripts/validate_tool.py tools/<id>
   cmake -B tools/<id>/build -S tools/<id> -DKIT_SDK_PATH=<kit>/tools-sdk
   cmake --build tools/<id>/build
   ```
3. Abra o PR. A CI (`validate.yml`) revalida e compila.
4. Revisão humana: código, conteúdo, permissões (`network` recebe atenção
   extra), aderência visual.
5. No merge, a pipeline empacota o `.kit`, assina com a chave de release
   (ADR-0012) e publica em Releases; o `index.json` é regenerado.

## Regras

- `id` em domínio reverso, único no catálogo; `com.kit.*` / `org.kit.*` são
  reservados.
- `version_code` inteiro, estritamente crescente entre versões.
- Atualização = novo PR do mesmo autor com `version` e `version_code` maiores.
- Ao contribuir, você concorda em licenciar a infraestrutura sob GPL-3.0; a Tool
  pode ter licença própria, declarada no seu README.

## Segurança

Vulnerabilidades: **não abra issue pública** — use o
[Advisory privado](https://github.com/jcrvlh/kit-tools/security/advisories/new).
