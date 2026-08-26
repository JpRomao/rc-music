# Evidência M0.2-preflight — toolchain Qt e fronteira de confiança

| Campo | Valor |
|---|---|
| `step_id` | `M0.2-preflight` |
| `requirements` | pré-condição operacional de M0.2 e preparação de `NFR-STACK-001–006`; nenhum FR/UX do shell é declarado concluído |
| `commit` | commit que adiciona esta evidência e o contrato `ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952`; identificável por `git log -1 -- docs/m0/evidence/m0-2-preflight.md` |
| `environment` | Windows 11 Pro x64 `10.0.26200`; PowerShell `7.6.5`; Qt `6.11.2` Desktop MSVC 2022 x64; MSVC `19.43.34808.0`; CMake/CTest `3.31.5`; Ninja `1.12.1`; `clang-format` `19.1.1`; NVDA portátil `2026.1.1` |
| `red_test_and_expected_failure` | o contrato começou ausente; o primeiro stage real expôs sharing violation do loader; a closure PE falhou pelo hash externo calculado com ordenação diferente da declarada; a suíte integral expôs variáveis Qt não cobertas e Authenticode dependente de `SystemRoot` hostil |
| `checks_run` | focuseds Red/Green; schemas e JSON canônicos; stage Qt físico; fronteiras pré-`CreateProcess` e pré-`ResumeProcess`; runner público Probe/VerifyReceipt; suíte integral `38/0`; harness M0.1 `273/0`; `CHK-DOC`; AST; `git diff --check` |
| `manual_checks` | nenhum check visual/NVDA manual executado; somente integridade, assinatura Authenticode e identidade do NVDA portátil foram comprovadas automaticamente |
| `artifacts_or_hashes` | contrato de 8 folhas `ac00a565…1952`; descriptor `1b688c1e…eaed`; golden `f5710e40…c909`; manifesto do stage `f45c0285…87f3`; closure QML `97b67eab…c44`; closure PE `4ec58c91…0a8f` |
| `checks_skipped_and_reason` | build/format/test do candidato, `CHK-ARCH`, `CHK-UI`, `CHK-A11Y` manual, `CHK-PERF`, `CHK-LINUX` e `CHK-PACKAGE`: o shell M0.2 ainda não existe; rede não foi bloqueada nem medida pelo SO |
| `residual_risk` | SBOMs Qt com `NOASSERTION` e licenças D3D/Mesa ainda bloqueiam distribuição; fingerprints de imports PE não versionam os TSVs-fonte nem o `dumpbin`; trust é cache-only; nenhuma aprovação de bundle ou jornada NVDA foi inferida |
| `decision` | Go somente para iniciar o shell experimental M0.2 por TDD; M0.2 permanece em andamento, nenhuma stack/engine foi aceita e M0.3 não foi iniciado |
| `next_step` | criar o candidato Qt isolado em `experiments/m0/candidates/qt`, começando por Red do core C++20 puro e do primeiro estado observável com backend falso |

## Escopo fechado

O preflight materializa e autentica somente os insumos do candidato Qt. Ele não cria código de produto nem shell, não toca áudio, não usa URL remota durante a validação e não produz decisão A/B. Downloads e toolchains grandes permanecem ignorados em `out/m0`; o Git preserva descriptor, schemas, manifesto, golden, contrato e testes suficientes para conferir os bytes locais.

O entrypoint confiável é `experiments/m0/provisioning/qt-shell/Test-M0QtShellToolchain.ps1`, executado em PowerShell limpo com um SHA-256 externo literal. O bootstrap retém contrato, sidecar, oito folhas e diretórios antes do import do módulo. `VerifyReceipt` exige também o hash externo dos bytes do receipt e não reexecuta os probes físicos. Ele fica colocalizado com o contrato de M0.2 para não ampliar o inventário congelado de `scripts/m0` pertencente ao harness M0.1.

## Evidência TDD e causal

| Incremento | Red causal | Green preservado |
|---|---|---|
| contrato, schemas e golden | folhas/âncora ainda ausentes e SHA externo inicialmente inválido | inventário fechado de oito folhas, schema estrito, serialização UTF-8/LF e golden byte-idêntico |
| processo e stage isolado | o stage inicial manteve writers incompatíveis com o loader Qt | transição de handles writer → bridge → read, Job kill-on-close e validações antes da criação, antes do resume e após a árvore esvaziar |
| adulteração causal | folha extra era rejeitada apenas por enumeração estática | mutação pré-criação impede alcançar o hook suspenso; mutação pós-criação/pré-resume mata o PID e não cria o TAP sentinela |
| closure PE | o teste Red esperou `ordinal-ignore-case`, mas recebeu `7b55af…`, produzido por outra ordenação | fingerprint externo corrigido para `2c70f3…`; 40 folhas, 115 arestas internas, 54 nomes externos e zero import Qt/System32 concreto ausente |
| ambiente e Authenticode | full suite recebeu quatro chaves Qt fora da allowlist e WinVerifyTrust falhou sob `SystemRoot`/`WINDIR` hostis | contrato cobre as chaves; trust usa o Windows root canônico e restaura exatamente o ambiente anterior |

Nenhum teste foi enfraquecido para aceitar o resultado. Os Reds relevantes permanecem como regressões no runner autocontido.

## Inventário e proveniência

O descriptor `qt-shell-toolchain.v1.json` contém URL literal, versão, tamanho, SHA-1 quando fornecido pelo repositório Qt, SHA-256 e estado de licença. O contrato externo fecha seus bytes e os do validador.

| Grupo | Identidade física comprovada | Licença/limite |
|---|---|---|
| Qt oficial | 7 archives: D3D compiler, Mesa software OpenGL, `qtbase`, `qtdeclarative`, `qtsvg`, `qttools`, `qttranslations` | tools Qt possuem binding físico GPL-3.0 + Qt exception/comercial; SBOMs de módulos seguem `NOASSERTION` e bloqueiam distribuição |
| Qt executável | `qmlformat`, `qmllint`, `windeployqt`, `qmltestrunner` `6.11.2.0`; 36 PEs selecionados | somente preflight local; nenhum bundle aprovado |
| host C++ | CMake/CTest `3.31.5`, Ninja `1.12.1`, `clang-format` `19.1.1`, MSVC `19.43.34808.0` | ferramentas observadas em caminhos explícitos e não empacotadas |
| testes | Catch2 `3.15.3`, archive `b0299ae5…c6e3` | BSL-1.0 fisicamente vinculada |
| extração | 7-Zip Extra `26.02`, executável `35d4d69d…59c2` | LGPL-2.1-or-later AND BSD-3-Clause AND BSD-2-Clause para os membros selecionados |
| acessibilidade | NVDA `2026.1.1`, portátil `2153e531…c899`, signer `4E9A772A99C1A3B713766DAC4B3545236CC2AE72` | GPL-2.0-or-later; assinatura/integridade comprovadas, jornada manual pendente |

## Stage e capacidades comprovadas

- inventário exato: `162` folhas e `59.226.130` bytes;
- closure QML: `119` folhas, `1.934.682` bytes e SHA-256 TSV `97b67eab…c44`;
- closure PE: `40` folhas, `57.667.264` bytes e SHA-256 TSV `4ec58c91…0a8f`;
- import graph: `115` arestas internas (`7f6289cc…916`), zero import Qt ausente, `54` nomes externos (`2c70f3b0…eee`), `16` API sets e `38` nomes concretos encontrados em System32;
- `qmlformat`: não mutação em check, normalização LF canônica e segunda passagem estável;
- `qmllint`: aceita fixture válida e rejeita sintaxe inválida, com exclusões nominais `QtDesignStudio` e `Quick`;
- `qmltestrunner`: TAP com QtQml Models/WorkerScript, Window, Controls Basic, Shapes, Qt Test e SVG;
- `windeployqt`: dry-run exato de `14` mappings, sem scan QML implícito e sem aprovação de staging;
- fechamento: o stage é removido antes de construir/publicar receipt e falha de cleanup impede evidência de sucesso.

## Checks executados

| Check | Comando/escopo | Resultado |
|---|---|---|
| runner público | `Test-M0QtShellToolchain.ps1 -RepositoryRoot <repo> -ExpectedContractSha256 ac00a565…1952` | Probe físico completo, receipt live schema-valid, exit code `0` |
| VerifyReceipt público | focused de receipt persistido com hash errado e correto em processos limpos | hash errado rejeitado antes do parse; válido retornou envelope idêntico; `1/0` |
| stage físico | materialização, quatro tools, capabilities, duas adulterações causais e cleanup | `1/0`; zero processo sobrevivente e root removido |
| descriptor/stage/evidence/golden | schemas, slots fechados, mutantes, canonicalidade e determinismo cross-process/culture/root | todos os focuseds verdes |
| suíte integral do preflight | `QtShellToolchain.Tests.ps1`, sem filtro | `38` passou, `0` falhou, exit code `0` |
| regressão M0.1 | `Invoke-M0HarnessTests.ps1`, sem filtro | `273` passou, `0` falhou, exit code `0`; manifest `3cc3889e…d490` e protocolo `d4f108e5…1404` preservados |
| `CHK-DOC` | `scripts/validate-docs.ps1` | `30` Markdown, `83` requisitos, `17` checks e `76` etapas; exit code `0` |
| AST | módulo, runner e testes | zero erro |
| whitespace | `git diff --check` | exit code `0` |

O hash do contrato é externo ao módulo e ao runner; o sidecar coincide, mas não substitui a âncora literal usada nos comandos acima. O runner foi mantido fora de `scripts/m0`, porque esse diretório integra o universo executável fechado de M0.1; o contrato M0.2 autentica sua nova localização sem reancorar o protocolo anterior.

## Checks não executados e riscos

- Não existe build Release, `CMakeLists.txt`, core C++20, QML do candidato, pacote ou medição; nenhum check de M0.2 foi antecipadamente marcado como aprovado.
- A jornada NVDA/teclado/alto contraste/escalas/movimento reduzido continua manual e pendente para o shell.
- O modo é `offline_inputs_only`: os probes não precisam de rede, mas não houve enforcement ou observação de egress no SO.
- O dry-run de deployment é diagnóstico; `staging_approved=false` e `package_approved=false` permanecem obrigatórios.
- Linux, VM Windows descartável, comparação A/B, performance e áudio continuam fora deste incremento.
- Os áudios pessoais autorizados pelo mantenedor não foram usados; o corpus do projeto continua separado e só começa em M0.5.

## Próximo passo seguro

Manter ADR-0001 `Proposto` e iniciar M0.2 no diretório experimental Qt. O primeiro incremento comportamental deve escrever um Red contra contratos públicos do core C++20 puro e do backend falso, produzir o mínimo Green, executar format/build/Catch2/CTest e só então acrescentar o primeiro estado QML. M0.3 permanece bloqueado até o shell Qt Release, seus gates manuais e a evidência consolidada de M0.2 estarem commitados.
