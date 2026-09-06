# Aplicação Windows — build de produto

**Estado:** unidade gráfica promovida; M1 e os gates de release permanecem abertos.
**Data:** 2026-09-06.

O build raiz agora compila o RC Music real, com Qt Quick, Qt Multimedia/FFmpeg e TagLib. Não encaminha para o CMake experimental, não autentica inputs M0 e não exige corpus, fixtures, JSONs de demonstração ou componentes Qt Test para produção. O [core independente](core-build.md) continua disponível sem Qt.

## Fonte única e fronteiras

- 95 arquivos da aplicação foram movidos para a raiz, com SHA-256 byte a byte preservado: headers públicos em `include/m0`, adapters, boundary e apresentação em `src`, bootstrap em `src/app`, QML/tradução em `src/ui` e gerador de ícone em `tools`.
- Os dois SVGs próprios em `assets` são cópias byte-idênticas das artes autenticadas. Os originais, manifests, protocolos e corpus históricos permanecem intactos. O consumidor histórico verifica também as cópias efetivamente embutidas.
- `cmake/RcMusicDesktop.cmake` define os mesmos alvos para o produto e o consumidor histórico. Somente o segundo injeta cenários, fake, recursos JSON e instrumentação. Ligar testes no build raiz não ativa essas capacidades na aplicação.
- O entrypoint `src/CMakeLists.txt` limita o scanner automático de imports Qt às fontes do produto; não varre a raiz com caches, experimentos ou toolchains.
- O nome técnico do alvo/executável continua `m0_qt_shell`/`m0_qt_shell.exe`. A marca visível é RC Music; namespace QML `MusicPlayer.M0`, recursos, tradução, IDs de dados e locks permanecem compatíveis. Isto não altera propriedades de distribuição ou migra dados.
- Oito arquivos de testes Qt foram movidos, sem cópias ou mudança de assertions, para `tests/qt`: abertura/argumentos, comandos e despacho IPC, coordenação de instância, codecs de preferências/sessão e seletores de áudio. Os testes históricos de presenter, UI, bindings e áudio ainda são consumidores do mesmo código promovido.

Incrementos posteriores: as [três suites de áudio](audio-tests.md) e a [cobertura de presenter/UI](ui-tests.md) passaram a integrar o CTest da raiz com entradas sintéticas próprias autenticadas. O consumidor histórico reutiliza essas mesmas fontes. A UI de produção e sua variante de teste usam uma única lista de fontes em `cmake/RcMusicUi.cmake`, mas plugins, objetos C++, registros QML e diretórios de recursos distintos. A macro que altera o layout C++ do presenter fica exclusivamente na variante de testes; não entra no executável real em ON/OFF. A raiz passou **276/276**; os registros abaixo preservam a evidência original da promoção desktop.

## Configure, build e testes

Para o fluxo diário, use os [comandos consolidados](development-commands.md): `scripts/develop.ps1 build` para produção e `scripts/develop.ps1 validate` para os checks da unidade promovida. O guia explica o arquivo de ferramentas e não exige preparar manualmente o ambiente MSVC. A receita explícita abaixo permanece como referência do build e da sua proveniência.

PowerShell 7 x64, na raiz. Use o [ambiente MSVC já provisionado](core-build.md#build-windows): CMake 3.31+, MSVC 19.44/toolset 14.44 e SDK Windows 10.0.22621.0. Os caminhos abaixo correspondem às dependências existentes no host, sem instalação, download ou reconstrução de SDK:

```powershell
$rcVsRoot = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools'
Import-Module (Join-Path $rcVsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $rcVsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
$rcRepo = (Resolve-Path .).Path
$rcNinja = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$rcQt = Join-Path $rcRepo 'q/f75eb9bea8280844c6d1a2b3f4/sdk'
$rcTagLib = Join-Path $rcRepo 'out/m0/taglib-2.3.1-msvc144-probe-20260829-01/install'
$rcMultimedia = Join-Path $rcRepo 'out/m0/qt-multimedia-archive-probe-20260829-01/stage/lib/cmake/Qt6Multimedia'
$rcQtHost = Join-Path $rcRepo 'out/m0/toolchains/qt-6.11.2-msvc2022_64'
$rcCatch2 = Join-Path $rcRepo 'out/m0/toolchains/Catch2-3.15.3'
$rcConfigure = @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DRC_MUSIC_BUILD_DESKTOP=ON',
    "-DCMAKE_MAKE_PROGRAM=$rcNinja", "-DCMAKE_PREFIX_PATH=$rcQt;$rcTagLib",
    "-DQt6Multimedia_DIR=$rcMultimedia", "-DQT_HOST_PATH=$rcQtHost")

cmake -S . -B out/product/desktop-release @rcConfigure -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'Configure de produção falhou.' }
cmake --build out/product/desktop-release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de produção falhou.' }
pwsh -NoProfile -File scripts/Test-ProductionBoundary.ps1 -BuildDirectory out/product/desktop-release
if ($LASTEXITCODE -ne 0) { throw 'Fronteira de produção falhou.' }
ctest --test-dir out/product/desktop-release -N

cmake -S . -B out/product/desktop-tests @rcConfigure -DBUILD_TESTING=ON "-DRC_MUSIC_CATCH2_SOURCE_DIR=$rcCatch2"
if ($LASTEXITCODE -ne 0) { throw 'Configure de testes falhou.' }
cmake --build out/product/desktop-tests --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build de testes falhou.' }
ctest --test-dir out/product/desktop-tests --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Testes falharam.' }
cmake --build out/product/desktop-tests --target all_qmllint
if ($LASTEXITCODE -ne 0) { throw 'QML lint falhou.' }
```

Qt **6.11.2** e TagLib **2.3.1** são exigidos exatamente; Catch2 **3.15.3** só entra com testes. `RC_MUSIC_BUILD_DESKTOP=OFF` é o default e conserva os comandos core-only. O desktop raiz recusa plataformas diferentes de Windows neste incremento, sem alegar validação Linux.

O executável produzido fica diretamente no diretório de build, mas não é um pacote autossuficiente: precisa das DLLs/plugins Qt/FFmpeg/TagLib existentes ou de runtime montado pelo builder local. O builder reconhece explicitamente os entrypoints raiz e histórico, usa o QML promovido e inclui `assets`/`tools` na proveniência. Não foi gerado novo runtime/instalador por esta promoção.

## Validação do incremento

- Produção raiz em `out/product/m1-desktop-release-off-20260906`: configure/build Release aprovados; grafo de 246 comandos com fontes reais presentes e fontes/includes experimentais, fake e cenários ausentes; CTest registra zero testes e o cache não encontra QtTest/QuickTest.
- Raiz ON em `out/product/m1-desktop-release-on-20260906`: configure/build Release e CTest **232/232**, em 10,06 s: 224 casos core e oito alvos Qt.
- Core-only, após reconfigurar com o novo entrypoint: build e CTest **224/224**, em 4,42 s; não encontra Qt.
- Exportação mínima de 150 arquivos em `out/product/m1-desktop-standalone-20260906/source`, contendo apenas CMake/módulos, headers, fontes, assets e ferramenta de ícone: configure/build Release OFF aprovados, sem diretórios `experiments` ou `tests`. Os 246 comandos usam as fontes exportadas, sem referências às fontes originais, experimentos, fake ou cenários. CTest registra zero testes.
- Consumidor histórico combinado, recompilado contra a fonte única: seleção CTest **21/21**, em 89,30 s, cobrindo os oito alvos promovidos, presenter, UI, bindings, restart, IPC, startup, identidade PE e áudio local. Presenter **265/265**, contrato UI **29/29** e Quick **187/187**, sem falhas/skips. Não é execução da suíte histórica completa.
- Equivalência: 95 arquivos de aplicação e oito de testes com hashes preservados; SVGs próprios preservam os bytes dos originais.
- API QML: arquivo gerado de 54.947 bytes idêntico na raiz ON/OFF e no consumidor histórico, SHA-256 `e8534f10016ff84190bfde8da909d3c692d96794328db1d05750cabdb20887df`, igual ao baseline anterior. A leitura PE do executável raiz OFF preserva ProductName/FileDescription RC Music e OriginalFilename técnico.
- `all_qmllint` da raiz e do consumidor histórico passou; formato C++ em 140 arquivos e QML em 29 arquivos aprovado, comparando stdout normalizado sem usar a opção inexistente `qmlformat --check`.
- Scripts de runtime/proveniência: Red 36 passagens/20 falhas pelos caminhos novos/API ainda ausente, seguido de Green **56/56**. Boundary: Red inicial 2 passagens/4 falhas; regressão adicional para cache duplicado; Green final **7/7**. Parsing PowerShell dos sete arquivos passou.
- Validação documental: 50 Markdown, 83 requisitos, 17 checks e 76 etapas; diff check aprovado. A revisão independente não encontrou bloqueadores remanescentes na extração do CMake.

A primeira configuração revelou que o scanner automático Qt usava a raiz inteira. O build foi interrompido e a configuração movida para `src`; o configure corrigido passou. Uma tentativa de reconfiguração sobreposta ao build ainda em encerramento falhou por lock de Ninja; o processo pertencente ao build foi encerrado e a execução serial posterior passou. Essas tentativas não são evidências de Green.

Movimentações mecânicas não mudam regras de negócio; os runners alterados têm Red/Green. Um probe adicional de recusa do argumento reservado nos executáveis raiz ON/OFF não foi executado: a ferramenta falhou ao criar o processo PowerShell com acesso negado pelo Windows. Não se declara resultado desse probe. O grafo de produção e os testes automatizados acima passaram; não houve nova observação nativa nem montagem/smoke de runtime.

## Próximos gates

Os [scripts raiz de M1.2](development-commands.md) estão consolidados, assim como a cobertura básica de UI/áudio. A [promoção de integrações Windows](windows-integration-tests.md) acrescenta quatro bindings e oito suites de adapters/stores/identidade PE; o CTest raiz passa 288/288, conservando quatro skips de symlink por política do host. Prosseguir com os smokes nativos e drivers do app instrumentado ainda separados, rastreabilidade e CI Windows de M1.3. M1.1 não encerra automaticamente preflight, arquitetura, segurança/licenças e fechamento global de M1. Preservar os gates funcionais, recuperação, acessibilidade nativa e distribuição ainda abertos no [plano](development-plan.md).

Não houve benchmark, A/B, Linux, instalação/associação no host, uso de áudios pessoais, Computer Use ou atualização do instalador a43faa1. A promoção não aprova release pública.
