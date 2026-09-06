# Comandos locais de desenvolvimento Windows

**Escopo:** runner de produto de M1.2 e diagnóstico local opt-in de M1.3; não encerra M1, CI remota ou release.
**Data:** 2026-09-06.

O entrypoint [scripts/develop.ps1](../scripts/develop.ps1) executa os checks da unidade de produto promovida, conforme o [ADR-0005](architecture/decisions/0005-windows-product-stack.md). O configure usa o CMake da raiz, não o shell experimental. Para as fronteiras de fontes, dependências e testes já promovidos, consulte o [build desktop](desktop-build.md).

## Ambiente e ferramentas explícitas

Execute em PowerShell 7 x64 no Windows. O arquivo de ferramentas padrão é `.tools/windows.psd1`, ignorado pelo Git. O [exemplo versionado](../tools/windows.example.psd1) contém os caminhos existentes no host validado; em outra máquina, ajuste-os para instalações locais equivalentes. Caminhos relativos são resolvidos a partir da raiz do repositório, não da pasta do arquivo de ferramentas.

O runner não instala nem baixa dependências. A configuração é um `.psd1` de dados com campos conhecidos: `CMake`, `CTest`, `Ninja`, `Compiler`, `ClangFormat`, `QmlFormat`, `VisualStudioRoot`, `QtPrefix`, `QtMultimediaPackage`, `QtHost`, `TagLibPrefix` e `Catch2Source`. Os caminhos exigidos dependem da ação; campos desconhecidos, caminhos inválidos ou ferramentas ausentes são recusados. `format` e `check` não exigem CMake, compilador, Qt de destino ou Catch2: exigem os dois formatters locais. Git deve estar disponível para os checks documentais e de diff; `rg` é opcional para a enumeração de fontes.

Versões desta receita:

- clang-format **19.1.1** e qmlformat **6.11.2**, conferidos por `--version` antes de formatar;
- Qt **6.11.2** e TagLib **2.3.1**, exigidos exatamente pelo CMake; Catch2 **3.15.3** somente para testes;
- MSVC/toolset **14.44**, SDK Windows **10.0.22621.0**, CMake **3.31+** e gerador Ninja.

Nas ações que compilam, o script entra no ambiente de desenvolvimento x64 do Visual Studio configurado. Não é necessário preparar manualmente o ambiente de compilação para esses comandos.

Pode-se usar o exemplo diretamente, sem criar configuração pessoal:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 check -ToolchainFile tools/windows.example.psd1
```

Para manter uma configuração local, copie o exemplo somente se o destino ainda não existir e depois ajuste os caminhos:

```powershell
New-Item -ItemType Directory -Path .tools -Force | Out-Null
if (-not (Test-Path -LiteralPath .tools/windows.psd1)) {
    Copy-Item -LiteralPath tools/windows.example.psd1 -Destination .tools/windows.psd1
}
```

## As seis ações

| `Task` | Operações, na ordem | Configuração de build |
|---|---|---|
| `format` | Verifica C++ e QML; só escreve com `-Apply` | Não configura nem compila |
| `check` | Formato, documentos, testes de rastreabilidade, manifesto CHK-TRACE, testes do runner, testes de diagnóstico, testes do formatter, `git diff --check` | Não configura nem compila |
| `build` | Configure e build do alvo real `m0_qt_shell` | Release, desktop ON, `BUILD_TESTING=OFF` |
| `test` | Configure, build e CTest | Release, desktop ON, `BUILD_TESTING=ON` |
| `lint` | Configure, build e alvo `all_qmllint` | Release, desktop ON, `BUILD_TESTING=ON`; não executa CTest |
| `validate` | Todos os passos de `check`, depois configure, build, lint e CTest | Release, desktop ON, `BUILD_TESTING=ON` |

`validate` é a ação padrão quando `Task` é omitido. `-Apply` só é aceito com `format`; não modifica implicitamente arquivos durante `check` ou `validate`.

A [rastreabilidade executável](quality/traceability.md) confere os 83 IDs, destinos, referências literais e estados, e oferece consulta inversa em JSON. Vínculos válidos não significam requisitos integralmente comprovados ou nova execução dos testes citados.

`-BuildDirectory` é opcional. O padrão de `build` é `out/product/desktop-release`; `test`, `lint` e `validate` usam `out/product/desktop-tests`. Caminhos relativos partem da raiz. As ações sem CMake não criam esses diretórios. `-Jobs` controla o paralelismo do build, com padrão 4 e intervalo de 1 a 16.

Exemplos a partir da raiz, usando `.tools/windows.psd1`:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 format
pwsh -NoProfile -File scripts/develop.ps1 format -Apply
pwsh -NoProfile -File scripts/develop.ps1 check
pwsh -NoProfile -File scripts/develop.ps1 build -Jobs 4
pwsh -NoProfile -File scripts/develop.ps1 test
pwsh -NoProfile -File scripts/develop.ps1 lint
pwsh -NoProfile -File scripts/develop.ps1 validate
```

## Formato sem alteração implícita

[Format-Source.ps1](../scripts/Format-Source.ps1) enumera `.cpp` e `.hpp` em `include`, `src`, `tests` e `tools`, e `.qml` em `src/ui/qml` e `tests/ui/quick`. Exclui segmentos `experiments` e `out`, inclusive se estiverem dentro dessas raízes. Usa `rg` primeiro e enumeração PowerShell quando `rg` não está disponível. Um inventário C++ ou QML vazio é erro, não sucesso.

O check C++ chama clang-format com `--dry-run --Werror`. O qmlformat **não oferece `--check`** nessa versão: o runner chama `--newline unix` e compara o stdout com a fonte, normalizando finais de linha para LF sem escrever no arquivo original. Uma diferença, inclusive a ausência de newline final, faz o check falhar. `-Apply` usa `-i` explicitamente nas duas ferramentas. Argumentos são passados em arrays, preservando espaços e Unicode nos caminhos.

## Recusas e propagação de falhas

Antes de configurar ou compilar, o runner exige um diretório filho de `out/product`, sem atravessar reparse points. Um cache existente precisa pertencer exatamente à raiz do produto e declarar Ninja, Release, `RC_MUSIC_BUILD_DESKTOP=ON` e o `BUILD_TESTING` da ação. Cache com entradas duplicadas, outra origem ou flags divergentes é recusado; o runner não apaga caches nem converte silenciosamente um build OFF em ON. Use diretórios distintos para produção e testes.

O cache também precisa corresponder às ferramentas selecionadas: CMake, compilador, Ninja, Qt, Qt Multimedia, host Qt, TagLib e Catch2 quando ON. Módulos Qt já encontrados precisam pertencer ao SDK configurado; pacotes de ferramentas podem vir do host Qt escolhido. Isso impede reaproveitar silenciosamente módulos de outro SDK apenas porque `CMAKE_PREFIX_PATH` foi alterado. Para trocar toolchain, escolha uma nova pasta de build; o runner não remove a anterior.

Cada processo precisa terminar com código zero. Falhas interrompem os passos seguintes e a mensagem final de sucesso não é emitida. O build sempre precede a execução dos testes; CTest recebe `--output-on-failure --no-tests=error`, portanto ausência de testes também é falha. O runner preserva os ambientes configurados por alvo no CTest, sem impor globalmente logging ou `QT_FATAL_WARNINGS`.

`lint` significa o `all_qmllint` real, além dos diagnósticos de compilação já configurados. Não representa clang-tidy, análise estática genérica, cobertura ou sanitizers ainda não integrados.

## Diagnóstico local opt-in

`-Diagnostics` preserva evidências da mesma execução, sem outro wrapper ou alteração dos checks escolhidos:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 check -ToolchainFile tools/windows.example.psd1 -Diagnostics
pwsh -NoProfile -File scripts/develop.ps1 validate -Diagnostics
```

Somente com essa opção o runner cria `out/product/validation-runs/run-<GUID>`. O destino e seus ancestrais são conferidos contra reparse points antes das operações; cada execução cria um diretório exclusivo, sem sobrescrever execuções anteriores. A opção exige uma raiz Git real com commit identificável. Recusa de parâmetros, destino inseguro ou impossibilidade de identificar/criar a execução pode ocorrer antes de existir um relatório. O comportamento padrão não cria esses artefatos; os testes do runner continuam usando e removendo suas próprias fixtures temporárias.

O [módulo de diagnóstico](../scripts/DevelopmentDiagnostics.psm1) grava:

- `run.json`, com versão do esquema, tarefa, diretório de build selecionado, commit e indicador dirty observados no início/fim, timestamps, resultado da execução e resultado/código de saída de cada etapa. Após leitura válida da configuração, inclui seu path/SHA-256 e o mapa das ferramentas resolvidas; o arquivo `.psd1` não é copiado e mudança de hash durante sua leitura é recusada;
- um log UTF-8 numerado por etapa, com stdout/stderr combinados, preservando inclusive a saída de um processo que termina com erro;
- `ctest.xml`, via `--output-junit`, somente quando a etapa CTest realmente executar. O path pertence à nova execução e precisa estar livre antes do comando; exit 0 exige um arquivo não vazio e sem reparse. Essa guarda verifica a entrega do artefato, não interpreta XML nem reavalia resultados. Não há cópia de `LastTest.log`, consulta aninhada do inventário ou reaproveitamento de relatório de uma execução anterior.

O preflight registra falhas de configuração de ferramentas/cache e preparação do ambiente antes de qualquer build. Falha de lançamento/preflight não inventa código de um filho: `exitCode` fica `null`; etapas que executam um processo conservam seu código real. O erro continua encerrando o runner com status não zero e impede etapas posteriores. Falha ao persistir o diagnóstico também não produz aprovação; quando já existe erro da ferramenta, ele permanece como causa principal e a falha de registro é avisada separadamente.

Os logs são gravados ao término de cada etapa, como a saída atualmente reunida pelo executor; não são um journal resistente a crash nem transmissão contínua. Encerramento externo pode deixar uma etapa `running`, relatório incompleto ou log vazio. Apenas um resultado final completo é evidência de término. Commit/dirty no início e no fim identificam o contexto observado, não uma captura imutável do working tree nem autenticação integral dos SDKs. `passed` registra o resultado dos comandos escolhidos, não aprovação de todos os requisitos ou da CI.

Não há coleta de ambiente, cópia da configuração pessoal, mídia, cookies, upload, compactação ou limpeza automática dos artefatos. Saídas das ferramentas podem conter caminhos locais, nomes de arquivos e mensagens de erro; os artefatos ficam ignorados pelo Git e devem ser revisados antes de compartilhar. A configuração de provedor/runner remoto e o provisioning de suas ferramentas permanecem pendentes; nenhum resultado de CI remota é inferido desta capacidade local.

### Evidência do diagnóstico

Em 2026-09-06, antes da implementação, o runner teve Red **31 passagens/3 falhas** pela ausência do novo passo em `check`/`validate` e do destino JUnit; a nova suíte teve Red **0/10** pelo módulo/API ausentes. Green inicial: runner **34/34** e diagnóstico **10/10**, com repositório Git sintético, diretórios Unicode/espaços, processos PowerShell próprios, código 23, stdout/stderr, fail-fast, erro de lançamento, preflight, dirty, isolamento de execuções, reparse e conflito real de gravação. O Git sintético desabilita hooks/templates externos e assinatura de commit somente nas próprias invocações, sem modificar a configuração do repositório real.

A revisão acrescentou Red **10 passagens/4 falhas**: dois casos de JUnit ausente/vazio aceitos após exit 0, configuração selecionada sem registro e aviso de gravação que mascarava o erro original sob `-WarningAction Stop`. Os avisos de recuperação agora usam `Continue` explicitamente; não alteram a política das ferramentas. Green final dos scripts: [diagnóstico **14/14**](../scripts/tests/DevelopmentDiagnostics.Tests.ps1) e runner **34/34**, incluindo uma invocação completa de `format -Diagnostics` com formatters PowerShell falsos, sem formatar fontes reais.

Um ensaio intermediário revelou um handle retido pelo próprio teste de conflito de gravação; o holder passou a usar referência compartilhada com cleanup em `finally`, preservando as assertions. O Green final removeu sua fixture. Esses testes dos scripts não executaram builds C++/Qt, formatters reais, CI remota, downloads, instalação, entrada nativa ou benchmarks; JUnit foi verificado pelo plano de argumentos e invoker falso, não por uma nova execução de CTest.

Na consolidação real do mesmo incremento, em 2026-09-06, foram executados separadamente:

```powershell
pwsh -NoProfile -File scripts/develop.ps1 test -ToolchainFile tools/windows.example.psd1 -BuildDirectory out/product/m1-desktop-release-on-20260906 -Diagnostics
pwsh -NoProfile -File scripts/develop.ps1 check -ToolchainFile tools/windows.example.psd1 -Diagnostics
```

- `test`: preflight, configure, build e CTest passaram. O JUnit foi lido independentemente: **297 elementos testcase, 297 testes, zero falhas e zero skips no nível CTest**, em 112,25 s no console. O log Qt preservado registra **quatro skips internos** de symlink por política do host, que o agregado CTest não representa. Quick passou **193/193**, incluindo a geometria com metadata/capa. Não se trata de nova execução de `validate`, lint ou produção OFF;
- `check`: formato real **172 C++/53 QML**, documentação **56 Markdown/83 requisitos/17 checks/76 etapas**, rastreabilidade **43/43** e manifesto com **83 vínculos válidos**, runner **34/34**, diagnóstico **14/14**, formatter **18/18** e diff passaram;
- os recibos ficaram em `out/product/validation-runs/run-873dbff09e164c47bdddbcbe48a4de07` (`test`) e `out/product/validation-runs/run-967701763d7748db8e48d9b60c0b8a19` (`check`). Ambos registraram `passed`, passos na ordem com exit 0, HEAD `3e81f93ff3780d288ffaf60853adbedd85a821ac` e dirty verdadeiro no início/fim: são evidências do incremento ainda não commitado naquele momento, não de checkout limpo. Configuração identificada pelo SHA-256 `c6a514241317414813313a01ee331fd1abd28d1a24d28caeab2c36de44347460`;
- logs de etapas sem saída, como preflight e diff aprovado, ficaram legitimamente vazios; configure/build/test e os checks com saída tiveram logs não vazios. Somente a execução `test` produziu JUnit. Os artefatos estão ignorados pelo Git; não foram incluídos no commit, publicados ou enviados a CI.

Revisão independente final sem achados P1/P2 remanescentes. O resíduo sintético de um ensaio intermediário, `out/product/diagnostics-contract-33761021fde343bf8e132a7ccc3f05c2`, permaneceu ignorado após uma tentativa de limpeza recusada; não houve contorno da recusa. Isso não é um artefato de validação aprovado nem impede os testes finais, que removeram suas próprias fixtures.

## Evidência do incremento

Em 2026-09-06:

- formatter: Red **0/13** pelo stub ausente; Green inicial **15/15**, usando ferramentas falsas, com caminhos Unicode/espaços, fallback sem `rg`, versões, falhas, check não mutante, `-Apply` e inventários vazios. A proteção do próprio harness teve Red **15 passagens/2 falhas**, seguido de Green **17/17**: confere GUID, destino e ancestrais antes de criar/remover sua raiz, incluindo recusa de junction em probe totalmente isolado;
- runner: Red **0/18**, seguido de Green inicial **18/18**. A validação de SDK teve Red **19 passagens/11 falhas** pela ausência do novo contrato de ferramentas, seguido de Green **30/30**. Com os contratos de configuração de ferramentas, o fechamento passou **33/33**, incluindo falha real de subprocesso e restauração do diretório corrente;
- execução real de `build` com `tools/windows.example.psd1` e `out/product/m1-desktop-release-off-20260906`: exit 0;
- execução real inicial de `validate` com o mesmo arquivo de ferramentas e `out/product/m1-desktop-release-on-20260906`: exit 0; formato **140 C++/28 QML**, runner **18/18**, formatter **15/15**, configure/build/lint e CTest **232/232** em 10,72 s;
- fechamento após as regressões: `validate` real no mesmo build ON passou novamente, com formato **140 C++/28 QML**, runner **33/33**, formatter **17/17**, configure/build/lint e CTest **232/232** em 12,12 s. O novo preflight também aceitou o cache real de produção OFF sem reconstruí-lo;
- validação documental final: **51 Markdown, 83 requisitos, 17 checks e 76 etapas**; revisão independente sem achados P0–P2 remanescentes e `git diff --check` verde.

Essas contagens descrevem as execuções observadas, não constantes usadas para aceitar inventários ou suites vazias. Nenhum código C++/QML foi alterado neste incremento.

## Limites

Os comandos usam dependências locais já provisionadas, mesmo quando seus diretórios conservam nomes históricos `out/m0` ou `q`. Não executam o harness M0, A/B, checks do protótipo descartado, download, instalação, empacotamento, benchmarks, Linux, Computer Use ou ensaios de dispositivos/áudio pessoal. O check documental pode ler registros históricos para validar links e contratos; isso não executa seus experimentos.

O build produz o executável real, não um instalador nem um runtime redistribuível. `validate` cobre a suite raiz atualmente promovida; não executa automaticamente toda a cobertura histórica de UI/áudio ou substitui os gates nativos, de acessibilidade, licenças, segurança e release. CI Windows de M1.3 e os demais itens de M1 permanecem separados no [plano](development-plan.md).

Incrementos posteriores: o mesmo `validate` passou a incluir as [suites de áudio e metadata](audio-tests.md) e a [variante exclusiva de testes de UI](ui-tests.md), com entradas sintéticas autenticadas somente em ON. A raiz passou **276/276 testes**, com formato **154 C++/52 QML**, runner **33/33** e formatter **18/18**. A inclusão dos QML de testes teve Red pela ausência no inventário e Green sem alterar os arquivos durante o check. As contagens da seção de evidência acima descrevem o incremento original do runner. Naquele checkpoint, bindings/plataforma e instrumentação de reinício de processo ainda não haviam sido promovidos; a aplicação real permanece sem cenários em ON/OFF.

O [incremento Windows seguinte](windows-integration-tests.md) promoveu oito suites de adapters/stores/identidade PE e quatro bindings. `validate` passou **288/288**, com formato **166 C++/52 QML**, runner **33/33** e formatter **18/18**; quatro casos internos de symlink foram ignorados por política do host, sem elevar permissões. Naquele recorte, o runner diário não incluía os quatro smokes QPA Windows nem os cinco drivers do app instrumentado. A leitura PE não executa o player; locks e privacy podem criar somente subprocessos do próprio teste, em isolamento.

A [promoção dos processos](process-tests.md) registra os cinco drivers em sete jornadas, mais uma guarda de ABI/recursos, usando dois shells instrumentados exclusivos de testes. Os gates de build/testes estão validados: `test` passou **296/296**, com build OFF, lint e formato executados separadamente, sem novo `validate` neste incremento. Após configure/build, `ctest --test-dir out/product/desktop-tests --output-on-failure --no-tests=error -L process` seleciona oito registros; `-L windows` passa a selecionar 19, incluindo as 12 integrações anteriores. Os quatro smokes QPA Windows continuam fora do runner diário, e o executável real permanece sem hooks em ON/OFF.
