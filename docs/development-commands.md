# Comandos locais de desenvolvimento Windows

**Escopo:** runner de produto de M1.2 e diagnóstico local opt-in de M1.3; não encerra M1, CI remota ou release.
**Data:** 2026-09-06.

O entrypoint [scripts/develop.ps1](../scripts/develop.ps1) executa os checks da unidade de produto promovida, conforme o [ADR-0005](architecture/decisions/0005-windows-product-stack.md). O configure usa o CMake da raiz, não o shell experimental. Para as fronteiras de fontes, dependências e testes já promovidos, consulte o [build desktop](desktop-build.md).

## Ambiente e ferramentas explícitas

Execute em PowerShell 7 x64 no Windows. O arquivo de ferramentas padrão é `.tools/windows.psd1`, ignorado pelo Git. O [exemplo versionado](../tools/windows.example.psd1) contém os caminhos existentes no host validado; em outra máquina, ajuste-os para instalações locais equivalentes. Caminhos relativos são resolvidos a partir da raiz do repositório, não da pasta do arquivo de ferramentas.

O runner não instala nem baixa dependências. A configuração é um `.psd1` de dados com campos conhecidos: `CMake`, `CTest`, `Ninja`, `Compiler`, `ClangFormat`, `QmlFormat`, `VisualStudioRoot`, `QtPrefix`, `QtMultimediaPackage`, `QtHost`, `TagLibPrefix`, `DataChannelPrefix` e `Catch2Source`. Os caminhos exigidos dependem da ação; campos desconhecidos, caminhos inválidos ou ferramentas ausentes são recusados. `format` e `check` não exigem CMake, compilador, Qt de destino ou Catch2: exigem os dois formatters locais. Git deve estar disponível para os checks documentais e de diff; `rg` é opcional para a enumeração de fontes.

### Qt Multimedia com FFmpeg corrigido

Com o SDK original e as DLLs qualificadas já disponíveis, execute uma vez na raiz do projeto:

```powershell
pwsh -NoProfile -File packaging/windows/New-RcMusicQtNetworkSdk.ps1
pwsh -NoProfile -File packaging/windows/New-RcMusicQtMultimediaSdk.ps1
pwsh -NoProfile -File scripts/develop.ps1 build -ToolchainFile tools/windows.example.psd1 -BuildDirectory out/product/desktop-ffmpeg-release
```

O primeiro comando prepara `out/product/qt-network-sdk`, uma cópia do SDK Qt com a DLL Qt Network qualificada. Use `-VendorSdkRoot` e `-DllDirectory` para indicar os insumos em outro host. O segundo comando prepara `out/product/qt-multimedia-6.11.2-rcmusic2` a partir do Qt Multimedia 6.11.2 e do FFmpeg em `out/product/fffix2/install/bin`. Ele aceita `-VendorSdkRoot` e `-FfmpegBinDirectory`. Em `.tools/windows.psd1`, use os caminhos retornados para `QtPrefix` e `QtMultimediaPackage` e configure um diretório de build novo. As ações `build`, `test`, `lint` e `validate` conferem os hashes qualificados antes de configurar ou compilar. [Proveniência do FFmpeg](delivery/windows-f8a593b-ffmpeg.md#uso-no-fluxo-windows)

### libdatachannel da sala de escuta

A sala de escuta ([ADR-0022](architecture/decisions/0022-listening-room.md)) usa libdatachannel **0.24.6** com Mbed TLS **3.6.8**. Mbed TLS é estático dentro de `datachannel.dll`, assim como libjuice, usrsctp e plog. O campo `DataChannelPrefix` aponta para a instalação, por padrão `out/product/libdatachannel-0.24.6`, e entra em `CMAKE_PREFIX_PATH` depois de Qt e TagLib. A opção CMake `RC_MUSIC_LISTENING_ROOM` vale `ON` no produto; com `OFF`, o build não procura a libdatachannel e não cria `rc_music_listening_room`. Só essa biblioteca linka `LibDataChannel::LibDataChannel`, e o CTest `rc_music.listening-room.build-boundary` confere a fronteira. O build Release copia `datachannel.dll` para o lado de `m0_qt_shell.exe`.

O runner não baixa nem compila essas dependências. Receita usada em 2026-11-18, em um prompt x64 do MSVC 14.44, a partir de `out/product`:

```powershell
git clone --depth 1 --branch v0.24.6 https://github.com/paullouisageneau/libdatachannel libdatachannel-src-0.24.6
git -C libdatachannel-src-0.24.6 submodule update --init --depth 1 -- deps/libjuice deps/usrsctp deps/plog
gh release download mbedtls-3.6.8 -R Mbed-TLS/mbedtls -D mbedtls-download-3.6.8 -p mbedtls-3.6.8.tar.bz2 -p mbedtls-3.6.8-sha256sum.txt
# Conferir o SHA-256 antes de extrair em mbedtls-src-3.6.8.
cmake -S mbedtls-src-3.6.8/mbedtls-3.6.8 -B mbedtls-build-3.6.8 -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_INSTALL_PREFIX="$PWD/mbedtls-3.6.8" -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON `
  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DGEN_FILES=OFF `
  -DMBEDTLS_USER_CONFIG_FILE="$PWD/mbedtls-3.6.8/config/rc_music_mbedtls_user_config.h"
cmake --build mbedtls-build-3.6.8; cmake --install mbedtls-build-3.6.8
cmake -S libdatachannel-src-0.24.6 -B libdatachannel-build-0.24.6 -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_INSTALL_PREFIX="$PWD/libdatachannel-0.24.6" -DCMAKE_PREFIX_PATH="$PWD/mbedtls-3.6.8" `
  "-DCMAKE_CXX_FLAGS_INIT=-DMBEDTLS_USER_CONFIG_FILE=<rc_music_mbedtls_user_config.h> -I$PWD/mbedtls-3.6.8/config" `
  -DUSE_MBEDTLS=ON -DNO_MEDIA=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON
cmake --build libdatachannel-build-0.24.6; cmake --install libdatachannel-build-0.24.6
```

O arquivo `mbedtls-3.6.8/config/rc_music_mbedtls_user_config.h` precisa existir antes do primeiro configure e contém exatamente duas linhas de configuração:

```c
#define MBEDTLS_SSL_DTLS_SRTP
#undef MBEDTLS_SSL_PROTO_TLS1_3
```

- `MBEDTLS_SSL_DTLS_SRTP`: a libdatachannel 0.24.6 chama `mbedtls_ssl_conf_dtls_srtp_protection_profiles()` no transporte DTLS mesmo com `NO_MEDIA=ON`.
- `#undef MBEDTLS_SSL_PROTO_TLS1_3`: o Mbed TLS fica sem `MBEDTLS_THREADING_C`. Não há backend de threads no Windows, e o Mbed TLS está estático dentro do `datachannel.dll`, sem como chamar `mbedtls_threading_set_alt`. O TLS 1.3 depende do estado global do PSA; por isso fica desligado, assim como `MBEDTLS_USE_PSA_CRYPTO`, que já vem desligado por padrão. O wss usa TLS 1.2, e o DTLS usa a versão 1.2, ambos com contextos próprios.

Mbed TLS e `datachannel.dll` precisam ser compilados com o mesmo arquivo. A configuração do Mbed TLS não chega aos consumidores, porque os headers da libdatachannel não incluem o Mbed TLS. Por isso, a conferência é feita na receita, depois do build. Nos dois comandos abaixo, o `dumpbin` não pode listar símbolos `psa_` não resolvidos:

```powershell
dumpbin /symbols mbedtls-3.6.8/lib/mbedtls.lib | Select-String 'UNDEF.*\| psa_'
dumpbin /symbols mbedtls-3.6.8/lib/mbedx509.lib | Select-String 'UNDEF.*\| psa_'
```

Versões, commits, hashes e licenças estão no ADR-0022.

Versões desta receita:

- clang-format **19.1.1** e qmlformat **6.11.2**, conferidos por `--version` antes de formatar;
- Qt **6.11.2**, TagLib **2.3.2** e libdatachannel **0.24.6**, exigidos exatamente pelo CMake. Catch2 **3.15.3** somente para testes. [Proveniência do TagLib](desktop-build.md#taglib-no-runtime-windows)
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
| `check` | Formato, documentos, testes de rastreabilidade, manifesto CHK-TRACE, testes do runner, testes de diagnóstico, testes do formatter, oito checks sintéticos das receitas Windows, `git diff --check` | Não configura nem compila |
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

## Checks das receitas Windows

`check` e `validate` executam os oito checks abaixo uma vez, sequencialmente, após `format-tests` e antes de `diff`:

| Etapa no runner/diagnóstico | Script |
|---|---|
| `runtime-plan-tests` | [WindowsRuntimePlan.Tests.ps1](../packaging/windows/tests/WindowsRuntimePlan.Tests.ps1) |
| `pe-dependency-tests` | [PeDependencyReport.Tests.ps1](../packaging/windows/tests/PeDependencyReport.Tests.ps1) |
| `runtime-recipe-tests` | [RuntimeRecipe.Tests.ps1](../packaging/windows/tests/RuntimeRecipe.Tests.ps1) |
| `production-boundary-tests` | [ProductionBoundary.Tests.ps1](../scripts/tests/ProductionBoundary.Tests.ps1) |
| `format-matrix-tests` | [FormatMatrix.Tests.ps1](../packaging/windows/tests/FormatMatrix.Tests.ps1) |
| `installer-plan-tests` | [WindowsInstallerPlan.Tests.ps1](../packaging/windows/tests/WindowsInstallerPlan.Tests.ps1) |
| `installer-sources-tests` | [InstallerSources.Tests.ps1](../packaging/windows/tests/InstallerSources.Tests.ps1) |
| `installer-recipe-tests` | [InstallerRecipe.Tests.ps1](../packaging/windows/tests/InstallerRecipe.Tests.ps1) |

O runner usa somente as entradas padrão, sem `-IncludeLegacy`. São testes de contratos com arquivos, Git e subprocessos PowerShell sintéticos; não executam player, NSIS, instalação, Registry ou compilação C++. `ProductionBoundary.Tests.ps1` usa Ninja falso, não inspeciona um novo build real. A compatibilidade histórica e os comandos de montagem/smoke/lifecycle continuam separados, conforme os guias de [runtime](windows-runtime.md) e [instalador](windows-installer.md).

As ações isoladas `format`, `build`, `test` e `lint` não acrescentam esses checks. `check` tem 16 etapas; `validate`, 20. Com `-Diagnostics`, o preflight é uma etapa adicional, e cada check executado recebe seu próprio log/resultado. Falha em qualquer um deles interrompe os seguintes, inclusive configure/build em `validate`; `check` não cria JUnit.

### Evidência da integração

Em 2026-09-06:

- runner: baseline **34/34**, Red **33 passes/4 falhas** pela ausência dos oito passos, Green **37/37**; ordem, paths absolutos/Unicode, entradas padrão e ações isoladas preservadas;
- diagnóstico: baseline **14/14**, Red **14 passes/9 falhas**, Green **23/23**. Invokers falsos comprovaram logs únicos, ausência de JUnit em `check` e falha em cada etapa nova de `validate`, preservando saída/códigos 23–30 e impedindo configure/build;
- a revisão encontrou cópia de caminhos do catálogo antes da validação em dois harnesses. Regressões inteiramente em GUIDs próprios produziram Red **14 passes/10 falhas** em `FormatMatrix` e **25 passes/10 falhas** no planner do instalador. Helpers locais agora planejam todo o inventário pelo módulo existente antes de criar/copiar, preservam tipos antes de concatenar caminhos e revalidam contenção, reparse e hashes. Green **24/24** e **35/35**; produto, catálogo e fontes NSIS não mudaram;
- `check -Diagnostics` real passou com **271/271 casos dos scripts**, incluindo **149/149** nos oito checks incorporados; formato **172 C++/56 QML**, documentação **58 Markdown/83 requisitos/17 checks/76 etapas**, 83 vínculos válidos e diff também passaram. Sintaxe dos cinco PowerShell alterados aprovada; revisão independente sem P0–P2 remanescente.

O recibo `out/product/validation-runs/run-d6a077deddec4ec88d982a552db91a3c/run.json` foi conferido independentemente: 17 etapas com exit 0 e logs presentes, sem JUnit; HEAD `3ef75902e66375668a689e9ac1a5908c1f76ae68` e dirty verdadeiro no início/fim, antes do commit deste incremento. O resultado identifica esse working tree, não um checkout limpo. Não houve novo `validate`, build C++/SDK, CTest, NSIS, runtime, instalação, Registry, Computer Use ou benchmark para integrar os checks.

## Formato sem alteração implícita

O inventário C++ é dividido em chamadas com orçamento de 30.000 unidades UTF-16, incluindo executável, flags e aspas, para respeitar o [limite de linha de comando do Windows](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw). Cada arquivo continua conferido uma vez; qualquer lote falho impede a etapa QML. A regressão de 2026-11-04 teve baseline 18/18, Red 18 passagens/3 falhas e Green 21/21; Check/Apply cobrem 182 paths longos com espaços/Unicode, sem perda, duplicação ou escrita no Check. A conferência real passou em 383 fontes C++ e 62 QML. Evidências: `out/product/test-runs/browser-media-discovery-20261104/format-source-*.log`.

[Format-Source.ps1](../scripts/Format-Source.ps1) enumera `.cpp` e `.hpp` em `include`, `src`, `tests` e `tools`, e `.qml` em `src/ui/qml` e `tests/ui/quick`. Exclui segmentos `experiments` e `out`, inclusive se estiverem dentro dessas raízes. Usa `rg` primeiro e enumeração PowerShell quando `rg` não está disponível. Um inventário C++ ou QML vazio é erro, não sucesso.

O check C++ chama clang-format com `--dry-run --Werror`. O qmlformat **não oferece `--check`** nessa versão: o runner chama `--newline unix` e compara o stdout com a fonte, normalizando finais de linha para LF sem escrever no arquivo original. Uma diferença, inclusive a ausência de newline final, faz o check falhar. `-Apply` usa `-i` explicitamente nas duas ferramentas. Argumentos são passados em arrays, preservando espaços e Unicode nos caminhos.

## Recusas e propagação de falhas

Antes de configurar ou compilar, o runner exige um diretório filho de `out/product`, sem atravessar reparse points. Um cache existente precisa pertencer exatamente à raiz do produto e declarar Ninja, Release, `RC_MUSIC_BUILD_DESKTOP=ON` e o `BUILD_TESTING` da ação. Cache com entradas duplicadas, outra origem ou flags divergentes é recusado; o runner não apaga caches nem converte silenciosamente um build OFF em ON. Use diretórios distintos para produção e testes.

O cache também precisa corresponder às ferramentas selecionadas: CMake, compilador, Ninja, Qt, Qt Multimedia, host Qt, TagLib, libdatachannel (quando o cache já o encontrou) e Catch2 quando ON. Módulos Qt já encontrados precisam pertencer ao SDK configurado; pacotes de ferramentas podem vir do host Qt escolhido. Isso impede reaproveitar silenciosamente módulos de outro SDK apenas porque `CMAKE_PREFIX_PATH` foi alterado. Para trocar toolchain, escolha uma nova pasta de build; o runner não remove a anterior.

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

## Limpeza de artefatos gerados — 2026-11-11

### Limpeza após a entrega de f8a593b

Foram removidos **3.737 arquivos (1.314.385.252 bytes lógicos)**. A seleção incluiu 1.158 intermediários declarados pelo Ninja, os dois setups substituídos de `8982c60` sem associações, 1.203 arquivos dos runtimes anteriores idênticos aos atuais, 1.368 cópias do SDK em uma pasta de execução visual e seis cópias temporárias de probes.

A conferência verificou 2.627 arquivos por SHA-256 e releu seis grafos Ninja. Foram preservados os setups e runtimes de `f8a593b`, os setups anteriores com associações usados na verificação de comparação, os executáveis de referência, os SDKs, os recibos e as sete alterações locais. O próximo build recompila os intermediários removidos. O plano e o resultado ficam em `out/product/cleanup-request-f8a593b-20261111/`. [Setups de f8a593b](windows-installer.md#setups-de-f8a593b)

### Limpeza após a entrega de 8982c60

Foram removidos **1.995 arquivos (1.157.657.216 bytes lógicos)**. A seleção incluiu os dois setups substituídos de `6d7ae8b` e 1.207 arquivos de seus runtimes, 569 intermediários declarados pelo Ninja, 12 cópias temporárias de probes, 203 arquivos gerados de um cache CMake cuja fonte foi removida e dois PDBs cujo conteúdo foi conferido no arquivo `.7z` preservado.

A conferência releu cinco grafos Ninja e verificou 8.098 entradas por SHA-256, incluindo os quatro setups atuais, os dois runtimes de `8982c60` e as alterações preexistentes. Os players de `6d7ae8b` ficaram como referências, com seus manifestos e recibos. Os SDKs ativos de `q` e `out/m0` foram preservados. Os planos e resultados estão em `out/product/cleanup-request-8982c60-20261111/`. Os próximos builds afetados regeneram os intermediários. [Setups atuais](windows-installer.md#setups-de-8982c60)

### Intermediários dos instaladores de 8982c60

Após conferir os [dois setups](windows-installer.md#setups-de-8982c60), foram removidos **496 objetos e bibliotecas intermediárias (278.580.879 bytes lógicos)** declarados nos grafos Ninja de `out/product/r7/out/product/{l,c}`. A limpeza conferiu por hash 16 arquivos protegidos, incluindo os setups, executáveis e manifestos, e releu os dois grafos. Plano e resultado em `out/product/test-runs/windows-candidates-8982c60-20261111/`.

### Limpeza após a entrega de 6d7ae8b

Foram removidos **7.288 arquivos (840.828.555 bytes lógicos)**. A seleção incluiu os dois instaladores substituídos de `35e999d`, 1.207 arquivos dos seus runtimes conferidos pelos manifestos, 449 intermediários e bibliotecas órfãs do build desktop e 5.630 arquivos de fixtures sintéticas recriadas pelos testes de empacotamento. Os executáveis do player de `35e999d` foram preservados como referências de comparação, junto dos manifestos e recibos.

Os [instaladores de 6d7ae8b](windows-installer.md#setups-de-6d7ae8b), seus runtimes, as extensões empacotadas, as dependências ativas e as alterações locais foram preservados. A conferência por SHA-256 da primeira etapa cobriu **7.209 arquivos**, e quatro grafos Ninja continuaram legíveis. Os planos, resultados e a remoção complementar de objetos Git somente leitura das fixtures ficam em `out/product/cleanup-request-20261111/`. Os intermediários removidos serão regenerados pelo próximo build. O checkout CMake/Ninja não possui manifesto ou cache Cargo local.

### Intermediários dos instaladores de 6d7ae8b

Após conferir os [dois setups](windows-installer.md#setups-de-6d7ae8b), foram removidos **496 objetos e bibliotecas intermediárias**, somando **276.191.535 bytes**. A seleção veio dos grafos Ninja de `out/product/r6/out/product/{l,c}`. A verificação reconferiu os hashes dos dois setups, players, manifestos e artefatos de build, além da leitura dos dois grafos. A seleção e o resultado estão em `out/product/test-runs/windows-candidates-6d7ae8b-20261111/`.

### Intermediários dos builds desktop

Foram removidos **700 arquivos (384.928.200 bytes lógicos)** dos builds desktop ON e OFF. A seleção incluiu objetos, bibliotecas estáticas e estatísticas QML declaradas pelo Ninja, além de duas bibliotecas antigas que ficaram no diretório raiz do build OFF depois de os alvos passarem para `src/`. O próximo build regenera os intermediários em uso. O checkout atual usa CMake/Ninja e não tem um projeto Cargo.

A verificação posterior leu **28 grafos Ninja** e conferiu **1.323 arquivos** por SHA-256, incluindo os dois instaladores e runtimes de `35e999d`, os pacotes Chromium e as seis alterações locais. SDKs, executáveis e recibos ficaram preservados. Os inventários da operação ficam em `out/product/cleanup-build-intermediates-20261111/`. A limpeza não executou builds, testes ou instaladores.

### Limpeza após a entrega de 35e999d

Após verificar os [dois novos setups](windows-installer.md#setups-de-35e999d), foram removidos **1.705 arquivos (910.896.023 bytes lógicos)**. A seleção incluiu os dois setups e 1.207 arquivos dos runtimes substituídos de `ebb093a`, além de 496 objetos e bibliotecas intermediárias declarados pelo Ninja nos builds de `r5`.

Foram conferidos **2.835 arquivos** por tamanho e SHA-256, incluindo os novos pacotes, os executáveis de comparação, os recibos e as alterações locais. Os dois grafos Ninja de `r5` continuaram legíveis. A seleção revisada e o resultado ficam em `out/product/cleanup-retired-35e999d-20261111/`.

### Limpeza complementar de builds antigos

Foram removidos **1.695 arquivos (240.392.710 bytes)**, entre saídas regeneráveis do Ninja, caches de dependências, metadados e fontes geradas pelo Qt, três bibliotecas órfãs e dois temporários do NSIS. Os builds históricos de M0 afetados precisarão de nova configuração CMake se forem retomados.

A conferência por SHA-256 preservou **1.305 arquivos**, incluindo os instaladores e runtimes de `ebb093a`, as dependências dos builds atuais, os recibos e as 24 alterações locais. Cinco grafos Ninja foram lidos após a remoção. A lista de arquivos e o resultado ficam em `out/product/cleanup-generated-20261111/`. O checkout usa CMake/Ninja e não tem saídas Cargo locais.

### Limpeza após a entrega de ebb093a

Foram removidos **1.528 arquivos (689.521.835 bytes)**. A seleção retirou os dois setups de `a469f8d` e seus payloads substituídos, 314 fontes geradas pelo Qt em builds antigos, três entradas sintéticas de testes de limite e duas páginas temporárias de download do NSIS.

Foram preservados os runtimes e instaladores de `ebb093a`, as dependências ativas, os pacotes Chromium reutilizados e os manifestos, recibos e dois executáveis usados como referência de `a469f8d`. Os intermediários removidos serão regenerados se os builds antigos forem retomados. A limpeza conferiu **1.284 arquivos** por tamanho e SHA-256, incluindo as seis alterações locais, e confirmou a leitura de **dez grafos Ninja**. A seleção e o resultado estão em `out/product/cleanup-retired-ebb093a-20261111/`.

### Builds dos instaladores de ebb093a

Após a inspeção dos [setups de `ebb093a`](windows-installer.md#setups-de-ebb093a), foram removidos **496 objetos e bibliotecas intermediárias**, totalizando **265.959.934 bytes**. A seleção usou as saídas `.obj` e `.lib` declaradas nos grafos Ninja de `out/product/r4/out/product/{l,c}`.

A limpeza conferiu **1.598 arquivos protegidos** por tamanho e SHA-256, incluindo os dois setups atuais, os anteriores, os executáveis e recibos. Os dois grafos Ninja continuaram legíveis. O plano e o resultado estão em `out/product/test-runs/windows-candidates-ebb093a-20261111/`.

### Builds dos instaladores de a469f8d

Após conferir os [setups de `a469f8d`](windows-installer.md#setups-atualizados-em-2026-11-11), a limpeza removeu **494 objetos e bibliotecas intermediárias**, somando **260.727.866 bytes**. A seleção se limitou às saídas `.obj` e `.lib` declaradas pelos grafos Ninja dos dois builds de empacotamento em `out/product/r3/out/product/{l,c}`.

A operação conferiu **1.771 arquivos protegidos** por tamanho e SHA-256, incluindo o trabalho em andamento, os executáveis, instaladores e recibos. Os dois grafos Ninja continuam legíveis. Os próximos builds desses caches recriam os intermediários removidos. Plano e resultado em `out/product/test-runs/windows-candidates-a469f8d-20261111/`.

### Remoção de versões substituídas e caches

A limpeza posterior removeu **6.107 arquivos (1.514.097.870 bytes)**. A lista inclui os quatro payloads de runtime de `7601247` e `26cc641` conferidos pelos manifestos, os quatro setups de `cf34cd8` e `26cc641` conferidos pelos recibos, 571 intermediários de build declarados pelo Ninja e 3.114 caches Python com fontes correspondentes. Os manifestos, configurações, recibos e logs das versões substituídas foram preservados.

Foram conferidos **1.291 arquivos** por tamanho e SHA-256, incluindo os setups e runtimes completos de `a469f8d` e as 26 alterações locais. Dez grafos Ninja continuaram legíveis. O plano e o resultado estão em `out/product/cleanup-superseded-20261111/`. A conferência foi feita por leitura de arquivos e grafos. Os próximos builds dos caches limpos regeneram os intermediários.

## Limpeza de artefatos gerados — 2026-11-10

Foram removidos **968 arquivos de build (179.047.853 bytes)**. A lista inclui 150 objetos do núcleo, 49 bibliotecas estáticas de builds auxiliares antigos e 769 fontes geradas pelo Qt em builds históricos. Os grafos CMake/Ninja, logs e recibos foram mantidos. Um build futuro recompila as saídas removidas.

A limpeza conferiu por SHA-256 o trabalho em andamento, os dois instaladores atuais, os manifestos e executáveis dos respectivos runtimes, uma DLL do SDK Qt, o executável de testes do núcleo e o recibo do ensaio nativo adiado. O inventário e o resultado ficam em `out/product/cleanup-20261110/`. Nenhum build, teste ou instalador foi executado durante a limpeza.

Uma segunda limpeza removeu **882 arquivos (330.217.885 bytes)** declarados nos grafos Ninja. Foram 427 objetos e 99 bibliotecas estáticas dos builds de empacotamento de `r1-src` e `r2`, além de 356 arquivos de estatísticas de compilação QML dos builds históricos de M0. A operação preservou os executáveis e perfis de empacotamento, os instaladores, o SDK Qt e os caches de desenvolvimento ativos. Foram conferidos por SHA-256 **200 arquivos protegidos** e foi confirmada a leitura de **nove grafos Ninja**, sem recompilar. O plano e o resultado estão em `out/product/cleanup-cache-20261110/`.

## Limpeza de artefatos gerados — 2026-11-05

A limpeza solicitada removeu **50.238 arquivos / 2.776.863.058 bytes** (aproximadamente **2,78 GB**, ou 2,59 GiB), sem novo backup de binários descartáveis:

- objetos CMake recompiláveis e intermediários de PyInstaller/DIA;
- 19 setups substituídos e os arquivos declarados de quatro payloads antigos, preservando manifests, configurações e recibos;
- seis stages de autoria cujos conteúdos já estavam reproduzidos no corpus versionado;
- 45.765 fontes duplicadas de `out/m0/ft50811-01/{qtbase,qtbase-patched,vulkan-headers-1.2.182.0}`, depois de comparar todos os hashes com as fontes canônicas e inventários em `q/96bvah3m0t0t71m990olv5gpvs/source`.

Foram conferidos antes/depois **2.706 arquivos protegidos**, incluindo fontes versionadas, seis WIP preexistentes, probes congelados, componentes pinados, os dois runtimes Native `7601247` e os dois setups `cf34cd8`. O SDK composto, Qt Host, Multimedia stage, TagLib install, fontes/licenças canônicas, mídia, logs e evidências permanecem disponíveis. A worktree `r1-src` foi mantida porque seus arquivos ainda aparecem como inputs dos runtimes atuais. Não há projeto Cargo ativo neste checkout.

A remoção usou uma lista literal revisada, recusou reparse points e preservou arquivos desconhecidos; diretórios foram retirados somente quando vazios. Os três grafos Ninja atuais continuaram legíveis. Código e índice Git permaneceram inalterados durante a limpeza, antes deste registro documental. Não houve build, CTest, setup, escrita no Registro ou retomada dos ensaios nativos adiados.

Recibos em `out/product/cleanup-20261105/`: `summary.json`, plano/resultado, proteção de arquivos e verificação das duplicatas. Os dois inventários maiores foram comprimidos em `file-inventories.zip` e reabertos para conferir tamanho/SHA-256 de cada entrada; `inventory-archive.json` conserva os hashes originais. Objetos removidos serão recompilados quando necessários. Reexecutar os experimentos FreeType antigos exige recriar suas cópias de fontes a partir das árvores canônicas preservadas. Entradas históricas dos guias descrevem a geração original; seus recibos não comprovam presença física de binários substituídos após esta limpeza.

## Limpeza local de intermediários — 2026-10-24

A limpeza solicitada pelo mantenedor removeu **10.701 arquivos `.obj`**, **6.957.596.342 bytes**, somente em `CMakeFiles` de builds antigos: `out/m0/builds`, caches auxiliares de `out/product` e `b` das duas execuções Qtbase `q/96bvah3m0t0t71m990olv5gpvs` e `q/hla2221pa11rncc9e2fhsta6r4`. Nenhuma raiz foi apagada recursivamente. Dois reparse points foram ignorados, sem percorrer seus destinos.

Foram preservados os builds desktop atuais ON/OFF, o backend TLS, SDKs/toolchains, fontes e licenças de terceiros, grafos/configurações CMake/Ninja, executáveis, bibliotecas, símbolos, runtimes, instaladores, fixtures, relatórios e evidências históricas. O RC Music aberto não foi encerrado. As compilações antigas limpas precisarão regenerar os objetos quando forem reutilizadas; os dois caches desktop atuais não foram limpos.

Antes da remoção, um ZIP foi criado sem sobrescrita e reaberto para conferir o SHA-256 de cada conteúdo. O backup ocupa **2.258.940.026 bytes**; a redução líquida é de aproximadamente **4,7 GB**, descontado o backup e antes dos pequenos recibos. Os artefatos locais ignorados ficam em `out/product/cleanup-20261024/`: `build-intermediates.zip`, `plan.json`, `backup-sha256.json` e `result.json`. O ZIP contém os caminhos relativos originais; para recuperar conteúdos, extraia primeiro em um diretório vazio, sem sobrescrever fontes ou builds em uso. Não é um backup completo do projeto.

Checks da operação: **823 arquivos versionados com hashes inalterados**, índice Git inalterado antes deste registro documental, seis âncoras CMake/Ninja preservadas e zero arquivos selecionados restantes. A seleção rejeitou paths não canônicos, arquivos versionados, reparse points e processos de compilação/teste ativos; cada original foi novamente conferido contra o hash do backup antes da remoção literal. Nenhum código de aplicação ou dependência foi alterado. Não houve build, CTest, benchmark, instalação, Registry ou validação de release; os resultados históricos acima não foram reexecutados por esta limpeza.

## Limpeza de duplicatas e artefatos obsoletos — 2026-10-24

Após autorização explícita para excluir os obsoletos e gerar primeiro o instalador, o [runtime e setup atualizados](windows-installer.md#instalador-local-gerado-em-2026-10-24) foram compilados e conferidos antes da remoção. A limpeza posterior excluiu definitivamente **58.079 arquivos / 7.975.164.119 bytes** (aproximadamente **7,98 GB de conteúdo lógico**), sem novo backup:

- fontes duplicadas da tentativa `q/hla2221pa11rncc9e2fhsta6r4/source`, ambas as instalações Qtbase antigas e binários/intermediários das duas árvores `q/*/b`;
- extração acidental `bin-placeholder` dos símbolos Qt, resíduos de compilação do leitor DIA e o diff numstat explicitamente excluído da evidência FreeType;
- binários recompiláveis de builds históricos selecionados, probes FreeType/TagLib e caches standalone, conservando configurações, grafos, fontes, logs, resultados e `Testing`;
- payloads `app` substituídos de `3f22fda`/`fc11d40`, mantendo manifests e `qml-imports.json`; resíduos sintéticos identificados também foram removidos;
- o `build-intermediates.zip` da seção anterior, com **2.258.940.026 bytes**. A recuperação por esse ZIP não está mais disponível.

Antes da exclusão, SHA-256 físicos confirmaram **45.765 fontes** da tentativa antiga idênticas à cópia mantida em `q/96bvah3m0t0t71m990olv5gpvs/source` e **4.289 arquivos** da instalação Qtbase bem-sucedida idênticos ao componente consolidado no SDK `q/f75eb9bea8280844c6d1a2b3f4/sdk`. Essa árvore de fontes permanece necessária ao builder TLS e à proveniência/licenças. Os SDKs/QtHost, Catch2, NSIS, Qt Multimedia stage, TagLib install, backend TLS, caches desktop atuais ON/OFF, novo runtime/setup e cópia `bfeb90f` foram mantidos. Pacotes, corpus, manifests e evidências únicas históricas ficaram fora da remoção.

A lista literal foi conferida contra o Git, os tamanhos/timestamps dos candidatos e caminhos canônicos confinados; reparse points não foram seguidos. Os dois links existentes sob `package-v2` foram ignorados e seus destinos não foram percorridos. Não houve remoção recursiva de raiz; diretórios selecionados foram removidos somente quando vazios. A guarda de processos recusou uma tentativa de planejamento antes de qualquer exclusão; o plano final foi salvo e a mutação ocorreu após as verificações próprias de processos/arquivos. A verificação de integridade foi executada em memória, sem alterar as dependências.

Resultado: **103.048 arquivos preservados com SHA-256 conferidos antes e depois**, **823 arquivos versionados e índice Git inalterados** durante a operação, e **zero candidatos restantes**. Depois da limpeza, `ninja -C out/product/m1-desktop-release-off-20260906 -n m0_qt_shell` retornou `no work to do`; é somente dry-run do grafo, não uma segunda compilação. O SHA-256 do novo setup permaneceu igual ao receipt. Os recibos locais ignorados ficam em `out/product/cleanup-20261024/`: `prune-plan.json`, `prune-result.json` e `installer-verification.json`; os pequenos registros da primeira limpeza também foram mantidos.

Os binários removidos exigem rebuild ou reextração de seus archives; cópias duplicadas podem ser reconstruídas a partir dos conteúdos preservados. Revalidar fisicamente os antigos installs, builds e payloads exige restaurá-los primeiro: seus receipts/manifests são históricos, não confirmação de presença após esta limpeza. O novo instalador não foi executado; não houve instalação, Registry, benchmark, áudio/Brave ou fechamento de release. Nenhum código de aplicação foi alterado.

## Limpeza após build do instalador — 2026-11-03

Operação posterior: a [entrega de 2026-11-04, `2144cfd`](local-delivery-2144cfd-20261104.md#limpeza-posterior), gerou primeiro os setups Lite/Completa e depois removeu 527.998.627 bytes de artefatos descartáveis, preservando as alterações incompletas, SDKs e pacotes conferidos. Seus recibos e limites são próprios; os registros abaixo permanecem históricos.

Após compilar e conferir o [novo setup de `531c5cc`](windows-installer.md#instalador-local-gerado-em-2026-11-03), a limpeza autorizada removeu **1.887 arquivos / 1.878.286.159 bytes** de binários antigos. A lista literal contém somente executáveis, DLLs, bibliotecas e intermediários recompiláveis em builds/packages históricos, cópias sintéticas de testes de instalação, caches auxiliares e payloads de runtimes substituídos. Os binários dos payloads `bfeb90f`, `1c79d81` e quatro cópias M0 antigas foram removidos; seus manifests, configurações, QML e registros históricos permanecem, sem constituir runtimes executáveis completos.

O plano rejeitou arquivos versionados, paths fora das raízes autorizadas, reparse points e componentes protegidos. Cada candidato teve tamanho/SHA-256 conferido antes da exclusão literal, sem exclusão recursiva de raiz ou novo backup. Duas junctions históricas foram ignoradas. **829 arquivos versionados e os 14 arquivos pendentes** permaneceram byte a byte iguais, assim como o índice Git durante a operação e o hash do novo setup. Nenhum candidato da lista permaneceu.

A cópia Git isolada desta geração foi removida (**830 arquivos / 14.650.416 bytes**); `ninja -t clean` removeu mais **365 intermediários / 59.993.762 bytes**. Somando as árvores inventariadas, foram removidos aproximadamente **1,95 GB de conteúdo lógico**. A revisão automática bloqueou a exclusão direta final do cache temporário, retornando apenas `blocked by policy`; restaram **211 arquivos / 9.385.583 bytes** em `out/product/installer-build-531c5cc-20261103`. O cache não é mais um build reutilizável sem reconfiguração, pois a worktree foi removida. A limpeza oficial do Ninja não substitui essa remoção final.

Preservados: SDK Qt consolidado, QtHost/ferramentas, Multimedia stage, TagLib install, Catch2/NSIS, fontes/licenças de Qt necessárias à proveniência/TLS, archives, protocolos/fixtures/evidências históricas, instaladores anteriores e o novo runtime/setup. Os caches desktop ON/OFF e `browser-integration-tests` usados pelo desenvolvimento pendente também foram mantidos. Dados pessoais e configurações do player ficaram fora da seleção.

Os planos, hashes e resultados locais ficam em `out/product/cleanup-20261103/`: `audit-proposed-files.json`, `prune-result.json`, `tracked-before.json`, `preserved-work.json`, `temporary-cleanup.json`, `installer-verification.json`, `executable-identity.txt` e `production-refusal.json`. Build Release, lint, inspeção do grafo, identidade PE e consistência do pacote são checks próprios; não houve suíte global nova, benchmark, instalação/Registry, áudio/Brave ou aprovação de release.

## Limpeza após atualização de f589040

Depois de compilar e verificar o [setup atualizado de `f589040`](windows-installer.md#instalador-atualizado-de-f589040), a nova limpeza autorizada removeu **268 arquivos / 174.025.804 bytes** de conteúdo lógico, sem criar backup:

| Raiz selecionada | Arquivos | Bytes removidos |
|---|---:|---:|
| `out/product/session-receipts-core` | 132 | 51.240.243 |
| `out/product/download-network-release` | 65 | 28.714.697 |
| `out/product/rc-music-local/rc-music-windows-dev-531c5cc-20261103/app` | 71 | 94.070.864 |

A seleção literal limitou-se a binários/intermediários regeneráveis dos dois caches de validação e ao runtime substituído. Os 71 binários antigos possuem contraparte no novo runtime; 70 eram byte a byte idênticos e apenas o executável mudou. Configurações, grafos, QML, manifests, resultados e logs permanecem; os caches limpos exigem recompilação para executar novamente seus testes.

Antes de cada exclusão foram conferidos path canônico, confinamento, ausência de reparse point e SHA-256. A guarda de processos não encontrou escritor/app ativo. Após a operação: **zero candidatos restantes**, **834 arquivos versionados** e índice Git inalterados, setup e inventário completo do novo runtime reconferidos. Evidência ignorada em `out/product/installer-refresh-f589040-20261103/`: `prune-plan.json`, `prune-result.json` e `independent-verification.json`.

Foram preservados SDK/dependências, caches ativos desktop ON/OFF e browser, backend TLS, fontes/licenças, fixtures/protocolos/evidências, todos os instaladores e dados pessoais. O cache temporário `installer-build-531c5cc-20261103` cujo delete anterior foi bloqueado não foi novamente selecionado. Não houve mudança de comportamento, nova suíte global, benchmark, instalação ou Registry.

## Limpeza após atualização de bb826b4

Após compilar e verificar o [instalador de `bb826b4`](windows-installer.md#instalador-atualizado-de-bb826b4), a limpeza autorizada removeu **2.141 arquivos / 307.200.630 bytes** de conteúdo lógico (aproximadamente 307 MB), sem backup:

| Seleção | Arquivos | Bytes removidos |
|---|---:|---:|
| Binários de `download-network-release` | 71 | 29.927.682 |
| Binários de dois probes Media Foundation | 17 | 1.312.481 |
| Binários substituídos de `c2d7c88/app` | 71 | 94.108.240 |
| Worktree/cache temporários `package-*-bb826b4` | 1.982 | 181.852.227 |

Os probes são `main-red-build` e `root-codec-probe-build` sob `out/product/test-runs/media-foundation-conversion-20261103`. Somente binários inventariados foram removidos desses caches e do runtime antigo; fontes, configurações, QML, manifests, logs e resultados permanecem. Os caches limpos precisam recompilar antes de nova execução. Cada binário antigo do runtime possui contraparte verificada no novo pacote.

Antes da exclusão foram conferidos caminhos canônicos confinados, ausência de reparse points, ignore do Git, tamanhos, hashes e ausência de writers. A worktree limpa foi removida pelo Git, e o cache temporário próprio após inventário. Preservaram-se snapshots de `CMakeCache.txt` e `.ninja_log`. Após a limpeza, zero candidatos restantes, **849 arquivos versionados**, **17 arquivos pendentes** e índice inalterados; 1.308 artefatos preservados foram reconferidos. A verificação independente completa do novo runtime/setup passou novamente.

SDKs/dependências em `q`/`out/m0`, caches desktop/browser ativos, backend TLS, fontes/licenças, protocolos/fixtures/evidências, todos os instaladores e dados pessoais ficaram preservados. A pasta `installer-build-531c5cc-20261103` anteriormente bloqueada permaneceu fora da seleção. Evidência ignorada em `out/product/installer-refresh-bb826b4-20261103/`: plano, script, inventário temporário, resultado e verificação posterior. Não houve mudança de comportamento, suíte global nova, benchmark, instalação ou Registry.

## Limpeza após atualização de c2d7c88

Após gerar e verificar o [novo instalador](windows-installer.md#instalador-atualizado-de-c2d7c88), foram removidos **1.746 arquivos / 257.312.609 bytes** de conteúdo lógico (aproximadamente 257 MB), sem backup:

| Seleção | Arquivos | Bytes removidos |
|---|---:|---:|
| Binários regeneráveis de quatro caches nativos | 251 | 78.078.992 |
| Binários do runtime anterior `f589040/app` | 71 | 94.108.752 |
| Worktree limpa e cache temporários `package-*-c2d7c88` | 1.424 | 85.124.865 |

As quatro raízes nativas são `native-peer-check-tests-20261103`, `native-peer-process-tests-20261103`, `native-pipe-reservation-tests-20261103` e `native-messaging-release-off-20261002`, sob `out/product`. Somente extensões binárias inventariadas foram selecionadas nelas e no runtime antigo; configurações, fontes geradas, QML, manifests, resultados e logs permanecem. Os testes desses caches exigem recompilação para nova execução. Cada binário antigo do runtime possui contraparte no novo pacote.

Paths absolutos, confinamento, ausência de reparse points, tamanhos e SHA-256 foram conferidos antes da exclusão. Não havia writer/app ativo. A worktree temporária limpa foi removida pelo Git; o cache temporário próprio foi removido após inventário, conservando cópias de `CMakeCache.txt` e `.ninja_log` na evidência. Os **847 arquivos versionados**, os **dois arquivos pendentes** e o índice permaneceram inalterados durante a limpeza; zero candidatos restantes. A verificação independente completa do novo runtime/setup passou novamente depois.

Foram preservados SDKs/dependências em `q`/`out/m0`, caches desktop ON/OFF e browser, backend TLS, fontes/licenças, protocolos/fixtures/evidências M0, todos os instaladores e dados pessoais. A pasta `installer-build-531c5cc-20261103` anteriormente bloqueada ficou fora da seleção. Nenhuma suíte global, benchmark, instalação ou alteração de Registry foi realizada. Evidência ignorada em `out/product/installer-refresh-c2d7c88-20261103/`: `cleanup-native-candidates.json`, `temporary-files.json`, `prune-result.json` e `after-cleanup-verification.json`.
