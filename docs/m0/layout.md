# Layout temporário dos spikes M0

**Estado:** contrato M0-L1 congelado; estrutura materializada até a matriz básica de codecs M0.5

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
│   ├── markers.v1.json
│   ├── markers.v1.sha256
│   ├── visual.v1.json
│   ├── visual.v1.sha256
│   ├── process.v1.json
│   ├── process.v1.sha256
│   ├── summary.v1.json
│   ├── summary.v1.sha256
│   ├── environments/
│   ├── goldens/
│   │   ├── pilot-intent.v1.json
│   │   ├── pilot-summary-input.v1.json
│   │   ├── pilot-summary.v1.json
│   │   ├── process.v1.json
│   │   ├── summary-input.v1.json
│   │   └── summary.v1.json
│   ├── scenarios/
│   │   ├── synthetic-process-tree.v1.json
│   │   └── synthetic-process-tree.v1.sha256
│   └── schemas/
│       ├── pilot-intent.schema.json
│       ├── pilot-summary-input.schema.json
│       ├── pilot-summary.schema.json
│       ├── process.schema.json
│       ├── process-golden.schema.json
│       ├── process-tree.schema.json
│       ├── process-tree-artifact.schema.json
│       ├── summary-contract.schema.json
│       ├── summary-input.schema.json
│       ├── summary.schema.json
│       └── synthetic-process-tree-scenario.schema.json
├── shared/
│   ├── shell-fixture/
│   └── audio-corpus/
├── audio-corpus/
│   ├── M0.AudioCorpus.psm1
│   ├── schemas/
│   └── tests/
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
    ├── M0.Evidence.psm1
    ├── M0.Markers.psm1
    ├── M0.Pilot.psm1
    ├── M0.Process.psm1
    ├── M0.Summary.psm1
    ├── M0.Visual.psm1
    └── tests/
        ├── EvidenceWriter.Tests.ps1
        ├── EvidenceSnapshotSet.Tests.ps1
        ├── Pilot.Tests.ps1
        ├── Process.Tests.ps1
        ├── Summary.Tests.ps1
        └── Visual.Tests.ps1
scripts/m0/
├── Invoke-M0AudioCorpusTests.ps1
├── Invoke-M0Pilot.ps1
├── Invoke-M0SyntheticProcessNode.ps1
├── New-M0AudioCorpus.ps1
├── Test-M0AudioCorpus.ps1
├── Test-M0Process.ps1
├── Test-M0Summary.ps1
├── Test-M0Visual.ps1
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

O contrato de processo M0.1 acrescenta descriptor/sidecar/golden e schemas fechados para identidade forte, atribuição, cleanup e artifact `process-tree`. `M0.Process.psm1` mantém a autoridade nativa em registry privado; o helper literal cria somente a topologia sintética root → child → grandchild. `Test-M0Process.ps1` valida o smoke Windows e o artifact P canônico apenas em memória, remove sua leaf de `out/m0/process-pilot` e não publica Evidence. Linux permanece `not_proven`.

O contrato de summary comum acrescenta descriptor/sidecar, input/golden e schemas fechados. `M0.Summary.psm1` transforma apenas input previamente validado e não acessa Evidence A/B; o golden prova cálculo e serialização canônicos em processos/roots independentes. `Assert-M0SummaryArtifacts` faz o check físico ancorado no Windows; a API pura permanece multiplataforma, enquanto o reader físico equivalente Linux continua `not_proven`.

O contrato separado do piloto ancora cenário, launch-intent, input/summary golden e schemas fechados. `Invoke-M0Pilot.ps1` publica no Windows uma Evidence `P` recuperável e não decisória sob um root novo de `out/m0`: writer e `run_started` precedem a árvore; `Close` precede o único artifact; intent/raw/run/snapshot precedem o summary; o sidecar do summary é a última publicação. O artifact canônico pós-cleanup é a fonte única das projeções. Nenhuma leaf P entra em snapshot-set, batch ou summary comum A/B. ETW live, breakaway, Linux, desempenho e power loss permanecem não provados.

## 4. Artefatos e Git

- JSON, Markdown, SVG e arquivos de hash versionados usam UTF-8 e LF.
- Schemas, fixtures, receitas, resumos e manifestos pequenos entram no Git.
- Builds, dependências baixadas, packages, ETL, dumps, screenshots em lote e raw ficam em `out/m0`; M0.1 cria a regra de ignore antes de materializar qualquer um deles.
- Cada conjunto bruto recebe manifesto SHA-256; o resumo commitado aponta para esse hash e informa onde o conjunto foi preservado.
- Evidências não contêm hostname, username, serial, biblioteca pessoal ou path absoluto.
- O protótipo perdedor é removido em commit explícito após M0.10 ou preservado somente se o ADR justificar seu valor diagnóstico.

## 5. Runners materializados e previstos

| Runner | Responsabilidade |
|---|---|
| `Test-M0Layout.ps1` | paths, arquivos esperados e isolamento |
| `Test-M0Fixture.ps1` | schema, determinismo, 1.000 itens, licenças e hashes |
| `Test-M0Markers.ps1` | catálogo confiável, cobertura dos cenários e policies temporais |
| `Test-M0Evidence.ps1` | cadeia JSONL, writer recuperável, snapshot commitado, projeções raw/run, agenda e batch fail-closed |
| `Test-M0Visual.ps1` | descriptor/sidecar/golden visuais ancorados e API sintética de ROI, detecção e UIA; não captura DXGI live |
| `Test-M0Process.ps1` | descriptor/golden/process-tree ancorados, processo limpo, Job Windows root → child → grandchild, cleanup escopado e artifact P somente em memória; Linux não provado |
| `Test-M0Summary.ps1` | descriptor/input/golden/schemas do summary comum, builder puro e trust físico Windows; não lê nem publica Evidence A/B |
| `Invoke-M0HarnessTests.ps1` | testes autocontidos do harness |
| `Invoke-M0AudioCorpusTests.ps1` | testes do manifesto, receitas/recibo, catálogo, bytes WAV/codecs, paths, links e geração segura em stage fresco |
| `Test-M0AudioCorpus.ps1` | `CHK-CORPUS` público sobre o corpus commitado e sua identidade física |
| `New-M0AudioCorpus.ps1` | geração determinística da fundação WAV somente em stage novo/vazio sob `out/m0`, sem rewrite do corpus commitado |
| `New-M0AudioCodecFixtures.ps1` | geração autenticada dos codecs somente em stage novo/vazio sob `out/m0`, com paths explícitos de FFmpeg/ffprobe |
| `Invoke-M0Pilot.ps1` | publicação recuperável Windows-only da evidência sintética `P`, com Job fechado antes do artifact, summary separado e zero participação em A/B |
| `Invoke-M0CandidateChecks.ps1` | checks nativos mapeados por candidato |
| `Invoke-M0ShellComparison.ps1` | preflight, ordem balanceada, coleta e resumo comum |

O harness usa `pwsh` 7.6.5 no host congelado e não depende do Pester global 3.4.0. Qualquer framework de teste adicional deve ser repo-local, pinado e justificado; a opção inicial é um runner autocontido que retorna exit code diferente de zero na primeira falha.
