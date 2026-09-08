# Hello

Tool de exemplo do catálogo KIT. Serve de referência para a estrutura de uma
submissão: `manifest.json`, `CMakeLists.txt`, `src/` e `icon.bin`.

No hardware, mostra um número de D20 no centro da tela e sorteia outro a cada
toque, com um bipe curto; a última rolagem fica salva via Storage API. No build
nativo (stubs), roda a lógica sem UI.

## Build local

```bash
# tools-sdk vem do repositório principal (jcrvlh/kit)
cmake -B build -S . -DKIT_SDK_PATH=/caminho/para/kit/tools-sdk
cmake --build build
./build/hello
```

## Empacotar

```bash
kit-cli validate .
kit-cli pack . -o io.github.jcrvlh.hello.kit
```

Licença: GPL-3.0 (mesma do catálogo). Uma Tool pode declarar outra licença no seu
próprio `README.md` e cabeçalhos de fonte.
