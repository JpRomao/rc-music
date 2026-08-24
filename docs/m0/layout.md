# Layout temporário dos spikes M0

**Estado:** congelado para M0.0

**Identificador:** `M0-L1`

O M0 é descartável por desenho e não constitui o scaffold do produto. Tudo fica isolado em `experiments/m0`; nenhum candidato cria `src`, `src-tauri`, `CMakeLists.txt`, `Cargo.toml` ou `package.json` na raiz antes da decisão M0.10.

## 1. Estrutura

```text
docs/m0/
├── README.md
├── environment.md
├── layout.md
├── protocol.md
└── evidence/
experiments/m0/
├── README.md
├── protocol/
│   ├── protocol.v1.json
│   ├── environments/
│   ├── scenarios/
│   └── schemas/
├── shared/
│   └── shell-fixture/
├── candidates/
│   ├── qt/
│   │   ├── shell/
│   │   ├── audio/
│   │   └── package/
│   └── tauri/
│       ├── shell/
│       ├── audio/
│       └── package/
└── harness/
    └── tests/
scripts/m0/
out/m0/
├── builds/
├── raw/
├── traces/
└── packages/
```

Diretórios só são criados quando a etapa correspondente começa. M0.1 cria protocolo executável, schemas, descriptors e fixture; M0.2/M0.3 criam apenas seus shells; M0.5 cria o corpus; M0.6/M0.7 criam apenas os adapters de áudio; M0.9 cria pacotes.

## 2. Limites

- `shared` contém dados, assets, strings, schemas e receitas, nunca biblioteca de produto compartilhada artificialmente entre C++ e Rust.
- Cada candidato consome inputs somente leitura e grava exclusivamente em seu build ou em `out/m0`.
- Nenhum candidato pode alterar fixture, protocolo ou ambiente durante uma execução.
- Runners mapeiam `Qt`/`Tauri` para paths literais com `ValidateSet` e `switch`; JSON nunca fornece comando executável.
- Não se usa `Invoke-Expression`, concatenação de shell ou path não resolvido.
- Todo path vindo de argumento é resolvido e precisa permanecer sob o diretório esperado.
- Assets locais não contêm URL, tracker, script remoto ou fonte pessoal.
- Backend falso, relógio e IDs são determinísticos.

## 3. Conteúdo comum M0.1

A fixture do shell congela:

- janela cliente de 960 × 640;
- estados `empty`, `playing` e `error`;
- strings pt-BR;
- temas sistema, claro e escuro;
- preferências autorizadas em D0.9;
- snapshots falsos a 5 Hz;
- preview/ack de seek determinísticos;
- fila com exatamente 1.000 IDs e ordem fixa;
- casos fixos de Unicode, texto longo e paths semânticos duplicados;
- zero arquivo de áudio, rede, URL, path real ou dado pessoal.

Cada asset recebe origem, licença e SHA-256. O corpus de áudio pertence somente a M0.5.

## 4. Artefatos e Git

- JSON, Markdown, SVG e arquivos de hash versionados usam UTF-8 e LF.
- Schemas, fixtures, receitas, resumos e manifestos pequenos entram no Git.
- Builds, dependências baixadas, packages, ETL, dumps, screenshots em lote e raw ficam em `out/m0`; M0.1 cria a regra de ignore antes de materializar qualquer um deles.
- Cada conjunto bruto recebe manifesto SHA-256; o resumo commitado aponta para esse hash e informa onde o conjunto foi preservado.
- Evidências não contêm hostname, username, serial, biblioteca pessoal ou path absoluto.
- O protótipo perdedor é removido em commit explícito após M0.10 ou preservado somente se o ADR justificar seu valor diagnóstico.

## 5. Runners previstos

| Runner | Responsabilidade |
|---|---|
| `Test-M0Layout.ps1` | paths, arquivos esperados e isolamento |
| `Test-M0Fixture.ps1` | schema, determinismo, 1.000 itens, licenças e hashes |
| `Invoke-M0HarnessTests.ps1` | testes autocontidos do harness |
| `Invoke-M0Pilot.ps1` | processo sintético e evidência sem candidato real |
| `Invoke-M0CandidateChecks.ps1` | checks nativos mapeados por candidato |
| `Invoke-M0ShellComparison.ps1` | preflight, ordem balanceada, coleta e resumo comum |

O harness usa `pwsh` 7.6.5 no host congelado e não depende do Pester global 3.4.0. Qualquer framework de teste adicional deve ser repo-local, pinado e justificado; a opção inicial é um runner autocontido que retorna exit code diferente de zero na primeira falha.
