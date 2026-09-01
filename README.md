# KIT — Catálogo Comunitário de Tools

Repositório de **Tools** distribuíveis para a plataforma [KIT](https://github.com/jcrvlh/kit).
Cada Tool é enviada por Pull Request, validada e compilada pela CI, empacotada
como `.kit` e publicada em Releases; um `index.json` estático descreve o catálogo
e é servido em **https://jcrvlh.github.io/kit-tools/**.

Não há servidor de aplicação — o catálogo é este repositório Git com automação.
Isso o mantém gratuito, auditável (todo pacote nasce de um PR) e reprodutível
(os `.kit` são construídos pela CI, não enviados prontos).

> A especificação completa está em
> [`docs/tools/registry.md`](https://github.com/jcrvlh/kit/blob/main/docs/tools/registry.md)
> do repositório principal.

---

## Estrutura

```
kit-tools/
├── tools/<id>/            uma pasta por Tool (id em domínio reverso)
│   ├── manifest.json
│   ├── CMakeLists.txt
│   ├── src/
│   ├── icon.bin
│   └── README.md
├── catalog/
│   ├── index.html         landing page do catálogo (servida no Pages)
│   └── index.json          GERADO pela CI — não versionado
├── schema/index.schema.json   JSON Schema do index.json
└── scripts/
    ├── validate_tool.py    checagens de PR
    └── build_catalog.py    gera o index.json
```

## Enviar uma Tool

1. Desenvolva com o [KIT Tools SDK](https://github.com/jcrvlh/kit/tree/main/tools-sdk)
   (`kit-cli new`, build nativo com stubs, simulador de desktop).
2. Crie `tools/<seu-id>/` seguindo o exemplo
   [`tools/io.github.jcrvlh.hello`](tools/io.github.jcrvlh.hello).
3. Abra um Pull Request. A CI roda `scripts/validate_tool.py` e compila a Tool.
4. Um mantenedor revisa; ao mesclar, a CI empacota, assina e publica.

Detalhes em [CONTRIBUTING.md](CONTRIBUTING.md).

## Namespaces de `id`

| Prefixo | Uso |
| :--- | :--- |
| `com.kit.*`, `org.kit.*` | reservados para Tools oficiais do projeto |
| `<seu-domínio-reverso>.*` | autores com domínio próprio (`br.com.fulano.jogo`) |
| `io.github.<usuário>.*` | autores sem domínio |

## Verificar o catálogo localmente

```bash
pip install ./relativo/ao/kit/tools-sdk/cli jsonschema
python scripts/validate_tool.py --all
python scripts/build_catalog.py         # gera catalog/index.json
```

## Licença

Infraestrutura sob [GPL-3.0](LICENSE). Cada Tool pode declarar a própria licença
no seu `README.md` e cabeçalhos de fonte.
