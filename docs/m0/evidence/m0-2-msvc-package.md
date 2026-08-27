# M0.2 — Integração do MSVC ao pacote Qt

**Data:** 2026-08-27
**Estado:** integração commitada em `63717e5`; pacote de receita limpa e smokes original/relocado validados; jornada nativa parcial; não encerra M0.2

## Escopo

Esta fatia integra à receita o [MSVC instalado em `d5c3027`](m0-2-msvc-installation.md). O objetivo é produzir um pacote novo com o shell atual, incluindo os atalhos de `707b031`, usando MSVC 19.44.35228.0 e o contrato separado `eead467f5a597a72b6ff45812b40040db629232bc089d0c61c0dd64c73693f03`. O pacote histórico `tray-200b983-clean-01`, seu manifesto `a227bb2b…97dc99` e o preflight `ac00a565…1952` não são reescritos.

Preparação de `NFR-STACK-005`, `NFR-REL-006` e da parcela local de `CHK-FAST`/`CHK-SEC`/`CHK-PACKAGE`. Os mesmos requisitos de shell e a fixture/protocolo congelados permanecem vigentes. Não é instalador, distribuição, ensaio A/B, reprodução de áudio real ou decisão de stack.

## Mudança de contrato da receita

- `Invoke-QtShellRefreshedBuild` valida o cache Release e gera receipt autenticado antes de chamar o build. No caminho de sucesso, repete as duas validações e retorna a configuração/receipt finais, inclusive quando CMake regenera legitimamente seus arquivos.
- Falha de autenticação inicial impede o callback. Falha nativa/exceção impede a autenticação final; erro nativo preserva saída e código. Drift de configuração/identidade posterior impede a criação do stage. O callback não publica arquivos de pacote.
- O entrypoint usa somente a identidade pós-build para compiler/linker/dumpbin, sem fallback para 19.43. Nome existente/inválido é rejeitado antes de construir, e o stage só é criado após build e verificações finais.
- Os pré-requisitos autenticam executores selecionados do descriptor histórico e exigem ambiente MSVC 14.44/SDK 10.0.22621.0 coerente. O ambiente herdado não é corrigido silenciosamente.
- O manifesto conserva `toolchain_contract_sha256` histórico e acrescenta hashes separados do contrato e do receipt de refresh, revisão do linker/toolset e observações do ambiente. `recipe_dirty` inclui a receita e o contrato novo.
- `evidence/msvc-refresh-receipt.v1.json` fica fora do payload, vinculado pelo manifesto. A verificação do payload não implica revalidar automaticamente esse arquivo externo.

As validações não pretendem atestar todos os inputs transitivos de compilação. O descriptor histórico não possui pin individual de `qmlimportscanner`; codegen, headers/libs e DLLs auxiliares não são integralmente autenticados por esses quatro executores. O limite quiescente permanece: não existe garantia contra um escritor concorrente hostil.

## TDD e checks

O Red real do entrypoint usou o build instalado com 14.44, antes da mudança da receita: exit **1**, `Compiler drift requires review of the pinned package recipe.`, sem criação do stage. Log: `out/m0/builds/qt-shell/msvc-14.44-install-check-20260827-01/recipe-refresh-red-01.log`.

O Red unitário de `RefreshedBuild.Tests.ps1 -Filter 'successful refresh*'` foi **0/1**, por ausência de `Invoke-QtShellRefreshedBuild`, antes da implementação. O Green passou **9/9**: ordem/parâmetros, callback único, falhas pré/build/pós, cache inválido antes/depois, regeneração legítima e ausência de publicação. O receipt é falso nesses nove casos; a autenticação real permanece coberta separadamente pelos testes de identidade e pelo exercício integrado.

- Regressões existentes: Builder **14/14**, identidade de refresh **16/16**, manifesto **113/113**, smoke com fakes **38/38**; total **181/181**.
- Pré-requisitos: Red focal **0/1**, exit 1, contra o stub que lançava `Package environment validation is not implemented.`, seguido de **16/16** verdes, repetidos pelo agente principal. O Red ficou na saída da ferramenta, sem log separado. Cobrem ambiente incompleto/divergente, flags injetadas, paths ambíguos/reparse, cache Ninja/RC/MT, pins e os oito membros históricos. Com os nove testes de orquestração, o total é **206/206**.
- Formato: **43 C++ e 22 QML**, sem divergências. Nenhum arquivo C++/QML foi alterado nesta fatia.
- Contrato histórico e oito membros: tamanho/hash reconferidos. Manifesto histórico: **1.378 arquivos / 82.388.347 bytes**, íntegro.
- Getter real no Developer PowerShell selecionado: quatro executores autenticados e snapshot dos 12 campos permitidos, sem dump de `PATH` ou credenciais. Parser dos **12** scripts/módulos do empacotador: zero erros. PSScriptAnalyzer não instalado; parser não é análise estática equivalente.
- Dois negativos do entrypoint real passaram: cache histórico com compiler 19.43 e ambiente novo com `CL=/DQT_M0_EXPECTED_REJECTION`. Ambos rejeitaram antes do build/stage; hashes de cache, `.ninja_log` e executável ficaram iguais antes/depois. A flag foi limitada ao processo do teste e restaurada.
- Revisões independentes de código/contrato: sem achados acionáveis, com os limites transitivos e quiescentes preservados.

## Build isolado

O diretório novo `out/m0/builds/qt-shell/msvc-14.44-package-refresh-20260827-01` foi configurado em Release, com Qt ON, testes ON, comandos de compilação exportados e paths explícitos de cl/link/Ninja. CMake identificou 19.44.35228.0. O ambiente selecionou MSVC 14.44.35207 e SDK 10.0.22621.0. Os probes pthread negativos e a ausência de `WrapVulkanHeaders` não impediram configurar; não se alega suporte Vulkan.

O build completo terminou em **277 ações**, `qmllint` sem diagnósticos e CTest **65/65**, zero falhas/skips, 16,32 s. A fonte foi `d5c3027`, sem alteração de shell/fixture/protocolo durante o build. As mudanças desta integração são PowerShell/documentação. O build foi feito antes do empacotamento; a reconstrução incremental solicitada pela receita respondeu `ninja: no work to do.`. Não se atribui à receita uma recompilação do zero que ela não realizou.

O core foi validado separadamente em `out/m0/builds/qt-shell/msvc-14.44-core-refresh-20260827-01`, Qt OFF: configure 2,07 s, build 125 ações/64,83 s, CTest **55/55** sem skips/1,36 s e execução direta **1.849 asserções**. Os 113 comandos de compilação, cache, regras de link e 14 imports PE examinados não têm dependência Qt. Catch2 local foi comparado integralmente ao tar autenticado: 586 arquivos, sem extras/reparse. O primeiro verificador temporário esperava `CMAKE_EXPORT_COMPILE_COMMANDS:BOOL`; CMake gravou `UNINITIALIZED=ON`. O diagnóstico original foi preservado, e a auditoria read-only corrigida passou sem rebuild/retest. Isso não alterou a suíte nem o cache.

Nenhum build ou stage histórico foi sobrescrito. Os comandos atuais estão no [README do candidato](../../../experiments/m0/candidates/qt/README.md).

## Pacote diagnóstico e execução real

Stage novo: `out/m0/packages/qt-shell/msvc-refresh-diagnostic-20260827-01`, fonte `d5c3027579d63679f6f50c94d720cf6cd29f8f45`, `source_paths_clean=true`, **`recipe_dirty=true`**. Este stage valida a integração antes do commit; não é o pacote identificado de receita limpa para os próximos gates.

- Manifesto SHA-256: **`3c656dc5e6200eed5c47b71d6ed848761659a69c4f9b1c58ad4cfecef6c574c1`**.
- Payload: **1.378 arquivos / 82.423.288 bytes**, 1.372 mapeamentos do deployment e **69 PEs x64**. Scan/cópia QML, inventário exato, hashes de origem/destino e imports foram validados pela receita.
- Shell `19.44.35228.0`, linker `14.44.35228.0`, Qt build `19.44.35227`, toolset `14.44.35207`. O receipt pós-build foi conferido contra o hash do manifesto: **`23207bd6972b90758cfe9e9599f0e8b6b4c8f51dc351ce1398d56853f37ad302`**.
- Em relação ao stage histórico, mudaram somente `m0_qt_shell.exe`, `qml/MusicPlayer/M0/Main.qml` e `LOCAL-EVALUATION.txt`; as bibliotecas Qt permanecem iguais. A diferença inclui os atalhos anteriores de `707b031`, não uma mudança QML nesta integração.
- Após confirmar ausência de RTSS e players de teste, o runner inalterado passou `tray-lifecycle` e `error-recovery`: exit 0, stdout/stderr vazios, sem encerramento forçado. Capturas completas dos módulos exigidos em 185,07/98,79 ms, com 5/7 amostras; origens observadas somente pacote/System32. Manifesto íntegro antes/depois, sem processos residuais do player.

Os tempos são observações diagnósticas, não benchmark M0.4. São jornadas offscreen/software/Basic; não comprovam renderização, foco, bandeja ou acessibilidade nativos. CRT permanece externo e nenhuma biblioteca do Windows foi copiada para o pacote.

### Artefatos locais

Paths relativos a `out/m0/builds/qt-shell/msvc-14.44-package-refresh-20260827-01/`, fora do Git:

| Artefato | SHA-256 |
|---|---|
| `configure.log` | `6ad3293b1e68d4fdff7774978757d995e5f26b70d8820eafcc747c6139057219` |
| `build-full.log` | `60689e43fb1ffb225dcc864fe9c32be517e0350594eca8cc1c0fdea3ce2ed672` |
| `qmllint.log` | `1b546eda0f5ad82c4e4a30238a354673f5a6fba1039e9a37bb99e8b8f0274e63` |
| `ctest-release.log` / `.xml` | `d54bbc3d69fbdc1a318584ad2f4c095045416e65c1e40d8bade3e04cc0045598` / `028fa682e3859fd68449dce6205e698bdba3fafec4219af607be2c0373b2c9eb` |
| `recipe-negative-results.json` | `50624d9b250ae63fc368594ad850a7cebf9e2480235a8d791ac58a71d7a89168` |
| `smoke-diagnostic.json` | `311bdc2559c4c82f081b23d533ea10833c6d767a0e6e9e52b7f312a66dbc5040` |

Os seis logs `ps-*.log` guardam as suítes do empacotador. Evidências do core estão em `out/m0/test-runs/qt-core-msvc14.44-refresh-20260827-01/`: `summary.json` SHA-256 `3ee68be19539381568b4b4b4babdd318250b8037a19dfe3c8652e173e16a29b9` e `ctest-core.junit.xml` `80f13f7ea351dad85f62747a428da29ecba5aa7dfb2525be0170eb73bbe66ea9`.

## Pacote de receita commitada

O commit **`63717e5b8935cfe64132f7653f5e457dfa9867f2`** fecha a implementação, testes e documentação da integração. Com o worktree limpo antes/depois, a receita gerou `out/m0/packages/qt-shell/msvc-63717e5-clean-01`: `source_paths_clean=true`, **`recipe_dirty=false`**, manifesto SHA-256 **`54eba3d9809dc1eae4b9b309d09d02721a86513bcc977638dea21101320cf787`**. O build incremental não precisou recompilar; fontes e bytes do payload são iguais aos do diagnóstico, com **1.378 arquivos / 82.423.288 bytes / 69 PEs**.

Uma cópia nova de `app/`, `evidence/`, manifesto e sidecar foi criada em `out/m0/packages/qt-shell/relocação com espaços 63717e5-01`. Não houve sobrescrita. Os dois manifestos e os vínculos externos de receipt, deployment e PE closure foram reconferidos, assim como os seis hashes de arquivos da receita commitada. O receipt mantém `23207bd6…ad302`; a proveniência do pacote não depende apenas do sidecar copiado.

O runner de `63717e5` passou nas quatro jornadas, sem RTSS ou relaxamento da whitelist:

| Local / jornada | Exit | Captura completa (ms) | Amostras |
|---|---:|---:|---:|
| Original / `tray-lifecycle` | 0 | 180,05 | 7 |
| Original / `error-recovery` | 0 | 103,96 | 10 |
| Relocado / `tray-lifecycle` | 0 | 107,56 | 10 |
| Relocado / `error-recovery` | 0 | 78,61 | 9 |

Todos terminaram com stdout/stderr vazios, zero erros transitórios de captura e sem encerramento forçado. Módulos observados somente do pacote/System32; o manifesto foi validado antes/depois em cada localização. Não restaram players após esses smokes. Os tempos não são métricas comparativas. O stage histórico `tray-200b983-clean-01` também foi revalidado integralmente sem alteração.

Relatórios no diretório de build desta fatia:

| Arquivo | Bytes | SHA-256 |
|---|---:|---|
| `smoke-clean-original.json` | 40086 | `b299361866fd2cb560ae24c0b42a010682f49c09809aa8e5edcb104a12f385dc` |
| `smoke-clean-relocated.json` | 40955 | `85d7872e11508a0e384e1037740520ed0dcba7747f4b49260718d3a9e4263c03` |
| `smoke-clean-summary.json` | 1427 | `5472ef0edb6214fa83057e8ae15334d251ae3be198ec040a728856a388746262` |

Os relatórios original/relocado foram gravados em `2026-08-27T18:05:23.6746955Z` e `2026-08-27T18:05:31.8648957Z`, respectivamente, conforme timestamps dos arquivos. Não são timestamps internos de início dos processos.

## Tentativa nativa deste pacote

O executável do pacote limpo foi aberto pela skill `computer-use`, com seleção inequívoca da janela retornada (`96077226`, título `Player de música`). PID observado **26420**, início `2026-08-27T18:06:07.823128Z`. Foram usados somente a fixture falsa e os controles do player; nenhum áudio pessoal, configuração global do Windows ou aplicativo externo foi alterado.

As capturas acompanharam estado vazio → fixture tocando por `Ctrl+O` → pausa por Espaço; o transporte mudou para Reproduzir antes de EOF. `Ctrl+L` mostrou a fila, `Ctrl+,` abriu Preferências e a seleção Aparência → Claro mudou visualmente a paleta sem reiniciar. Isso não determina a causa das capturas divergentes da [tentativa histórica](m0-2-native-attempt-20260827.md), nem prova que o compilador corrigiu aquele problema.

A observação de foco via UIA permaneceu inconclusiva: a extração ainda indicava `current-title` após navegação, e algumas mudanças de controles apareceram somente numa captura posterior. A borda do botão Fila também representa seleção, não apenas foco. Não foi concluído defeito no produto ou na captura; os testes QML de foco não comprovam entrega de eventos ao leitor de tela.

A tentativa de Escape foi recusada pela ferramenta com `user input was detected in this window; call get_window_state before continuing`. A nova captura mostrou o menu de tema aberto, diferente do estado anterior; as entradas automatizadas foram pausadas e o usuário avisado. Na consulta read-only de `2026-08-27T18:14:26.9966374Z`, o PID não estava mais presente. Não houve kill pelo agente; causa e código do encerramento não foram observados. **Isso não aprova Escape, Ctrl+Q, Fechar, bandeja ou saída normal nativos.**

O manifesto do pacote continuou íntegro. O NVDA portátil teve apenas identidade/assinatura reconferidas; não foi iniciado. Não houve jornada NVDA, HC/DPI reais, movimento reduzido do Windows, menu/recuperação do Explorer ou teste Linux. A retomada das entradas depende de disponibilidade do desktop sem interferência no uso manual.

## Limites e sequência

Integração e pacote offscreen estão validados, mas a [triagem de segurança](m0-2-security-triage.md) continua aplicável no seu escopo: as bibliotecas Qt não foram atualizadas, e a instalação do compilador não corrige FreeType nem resolve servicing/redistribuição/licenças. A preparação da regressão e do backport upstream isolado é o próximo passo técnico dentro do M0 já autorizado; ainda não foi aplicada nesta evidência. Qualquer SDK corrigido exige nova identidade de fonte/patch/configuração e novos checks do pacote, sem alterar o histórico nem aceitar definitivamente uma stack.

NVDA, bandeja/Explorer, alto contraste e DPI reais, Linux, máquina limpa e medições continuam pendentes. A observação nativa parcial não encerra `CHK-UI`/`CHK-A11Y`. M0.3 não é liberado e nenhuma stack é aceita por esta integração. O commit de evidências posterior registra os resultados de execução sem alterar a receita ou o payload de `63717e5`.

Esse fechamento é documental, sem Red/Green artificial. A revisão independente dos dois pacotes, vínculos de evidência, seis arquivos da receita e quatro jornadas registradas não apresentou achados. O validador documental passou com **37 Markdown, 83 requisitos, 17 checks e 76 etapas**, e `git diff --check` passou. As suítes de `63717e5` não são apresentadas como repetidas nesta atualização documental.
