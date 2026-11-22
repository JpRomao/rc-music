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
- No Windows, o executável de aplicação usa `WIN32_EXECUTABLE TRUE`: abre como aplicativo gráfico sem terminal adicional. O entry point é fornecido pelo Qt; runners e probes de teste permanecem console. A [regressão de subsistema PE](windows-integration-tests.md#abertura-sem-terminal-adicional) confere os bytes do executável, inclusive no build OFF.
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
$rcQt = Join-Path $rcRepo 'out/product/qt-network-sdk'
$rcTagLib = Join-Path $rcRepo 'out/product/taglib-2.3.2-msvc144/install'
$rcMultimedia = Join-Path $rcRepo 'out/product/qt-multimedia-6.11.2-rcmusic2/lib/cmake/Qt6Multimedia'
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

Qt **6.11.2** e TagLib **2.3.2** são exigidos exatamente. Catch2 **3.15.3** só entra com testes. `RC_MUSIC_BUILD_DESKTOP=OFF` é o default e conserva os comandos core-only. O desktop raiz recusa plataformas diferentes de Windows neste incremento, sem alegar validação Linux.

O executável produzido fica diretamente no diretório de build, mas não é um pacote autossuficiente: precisa das DLLs/plugins Qt/FFmpeg/TagLib existentes ou de runtime montado pelo builder local. O builder reconhece explicitamente os entrypoints raiz e histórico, usa o QML promovido e inclui `assets`/`tools` na proveniência. Não foi gerado novo runtime/instalador por esta promoção.

## Qt Network no SDK Windows

A DLL do Qt Network 6.11.2 inclui os dois patches oficiais para a CVE-2026-116337 e as mudanças de produção do patch da CVE-2026-96432. Este último foi adaptado ao literal de texto da revisão local. A mitigação HTTP/1.1 das integrações de letras e Lidarr permanece ativa. [CVE-2026-116337](https://www.qt.io/blog/security-issue-cve-2026-116337?hsLang=en) · [CVE-2026-96432](https://www.qt.io/blog/security-advisory-cve-2026-96432)

O build RelWithDebInfo x64 recompilou `Network` e suas dependências a partir de 22.856 arquivos autenticados do snapshot Qtbase com o backport FreeType. As 1.007 opções Qt correspondem ao SDK de origem. A comparação de 71 PEs confirmou os exports existentes e a resolução dos imports do RC Music, Qt QML, Qt Quick, Qt Multimedia e Qt Core. O teste com respostas HTTP válidas em loopback obteve GET 200 e POST 201 e confirmou o carregamento da nova DLL. [Origem do Qtbase](m0/evidence/m0-2-qtbase-refresh.md)

O recibo de `q/n2/build-receipt.json` tem SHA-256 `b8aad24e7bd0a08c764bbfdce418c162b10e5650c214a46ff92836e33cc8bd06`. O preparador copia o SDK e substitui a DLL e os símbolos correspondentes. O runner e o empacotador exigem o hash da DLL qualificada e o manifesto do runtime registra o recibo. [Comandos de preparo](development-commands.md#qt-multimedia-com-ffmpeg-corrigido) · [Identidade da DLL](../packaging/windows/qt-network-runtime.v1.json)

A verificação física de 9.880 arquivos do SDK encontrou duas substituições, `Qt6Network.dll` e `Qt6Network.pdb`. A receita de runtime passou 26 testes e o runner passou 37. Os testes dos serviços de letras e Lidarr passaram em 68 casos com respostas simuladas e a DLL nova. Os resultados estão em `q/n2/sdk-stage-verification.json` e `out/product/qt-network-abi-check/app-services/`.

O build Release e a montagem Lite de `dd0e650` passaram pelo fluxo padrão. O manifesto SHA-256 `3c28cf0f8c5e5b3ade081d4dc79bfde6c655bb1f25a34638e81ae047114e05fa` identifica os 572 arquivos em `out/product/r9/out/product/qt-network-runtime/`. O smoke offscreen passou na inicialização e em 17 cenários de áudio, com 19 casos Qt e zero skips. O recibo fica em `out/product/test-runs/rc-music-runtime-smoke/20a1f86faea948db962114fad208b1ae/`. A verificação do instalador conferiu a DLL na seção obrigatória do plano Lite.

A receita atual inclui 19 avisos do Qtbase. Os 18 textos upstream foram conferidos contra o snapshot de Qtbase e a fonte do rebuild de Qt Network. O aviso identifica o backport FreeType, as correções do Qt Network e a origem do plugin Schannel restrito do worker. [Aviso no pacote](../packaging/windows/notices/qtbase/NOTICE.txt) · [Auditoria Qtbase](delivery/runtime-3f22fda-qtbase.md) · [Condições Qt](https://www.qt.io/development/open-source-lgpl-obligations)

### Fontes do Qt Base e Qt Network

O ZIP privado `out/product/qtbase-6.11.2-source-20261111/RCMusic-QtBase-QtNetwork-6.11.2-source.zip` reúne as fontes fixadas, patches, licenças, receitas, instruções e recibos. SHA-256 `9d0c3d537962094cba0d80c960c5c23dcbe646ec96ab952a00a8833d03b6c14a`. A verificação offline conferiu 22.856 arquivos Qtbase e 53 do Vulkan-Headers e reproduziu byte a byte o arquivo alterado pelo patch FreeType e os quatro arquivos alterados pelos patches Qt Network. O recibo de proveniência conferiu dez binários de cada edição de `ba112a6`, incluindo as duas ocorrências da DLL Qt Network.

O ZIP inclui um verificador e instruções de reconstrução. Equivalência de build, substituição ou relink, fontes dos demais módulos Qt e qualificação de distribuição continuam pendentes. [Fontes do plugin Schannel](packaged-download-profile.md#fontes-do-plugin-schannel-restrito)

### Avisos de Qt Declarative e Qt SVG

A receita inclui quatro textos de licença sem alterações e um aviso com os créditos do JavaScriptCore Macro Assembler, Yoga, sombras Angular Material e XSVG. Os metadados de atribuição conferem com o SPDX do SDK, e os cinco binários identificados têm os mesmos hashes nas duas edições de `ba112a6`. O recibo `out/product/qt-declarative-svg-notice-inputs-20261111/provenance.json` registra os blobs Git e os SHA-256 das entradas. A inspeção confirmou os arquivos nos dois setups com a receita de `cea27cf`. [Setups](windows-installer.md#setups-privados-com-a-receita-de-cea27cf) · [Aviso no pacote](../packaging/windows/notices/qt-declarative-svg/NOTICE.txt) · [Qt Declarative 6.11.2](https://github.com/qt/qtdeclarative/tree/4e3399c26ec57246c08de019cfcbda8d23604cfa) · [Qt SVG 6.11.2](https://github.com/qt/qtsvg/tree/17ca512f903f935282ebeca496aac5d11ba4199a)

O teste da receita detectou os cinco arquivos ausentes e passou em 26/26 casos depois da inclusão. A montagem Lite de `937ca73` conferiu os 645 arquivos no disco. O manifesto e a verificação em `out/product/r9/out/product/qt-declarative-svg-20261111/` registram os cinco avisos novos e a igualdade por SHA-256 dos 640 arquivos anteriores.

### Aviso dos ícones Phosphor

Os ícones SVG da interface vêm do Phosphor Icons, sob a licença MIT, e são compilados no recurso `:/rc_music/icons`. A receita do runtime local e o seu teste listam os dois arquivos de `notices/phosphor/`: o aviso, com a origem `phosphor-icons/core` no commit `2b75f3ad12b420c9504ef05df8d2564a28f8500e`, e o texto da licença sem alterações. Nenhum runtime ou setup foi montado com esses avisos até agora. [Aviso no pacote](../packaging/windows/notices/phosphor/NOTICE.txt) · [Phosphor Icons](https://github.com/phosphor-icons/core/tree/2b75f3ad12b420c9504ef05df8d2564a28f8500e)

### Runtime Lite com teclado e aviso Expat

A montagem Release de `0107bf2` inclui a navegação por teclado nas listas de [Lidarr](lidarr-integration.md#evidência-de-implementação) e [versões de letras](lyrics-variants.md#navegação-nas-versões-por-teclado). A conferência dos 646 arquivos encontrou o novo executável, a licença Expat no componente opcional e 644 arquivos com os hashes anteriores. O planejador vinculou a licença à opção de downloads nas cinco combinações e a configuração de produção registrou zero testes. Manifesto e recibos em `out/product/r9/out/product/keyboard-expat-20261111/`.

Essa montagem gera um runtime local. Os setups privados continuam na [entrega com receita de cea27cf](windows-installer.md#setups-privados-com-a-receita-de-cea27cf). A requalificação do parser XML do componente YouTube continua pendente. [Aviso e alcance do Expat](packaged-download-profile.md#aviso-expat-no-perfil-youtube)

## TagLib no runtime Windows

O build de produto e os alvos de teste exigem TagLib 2.3.2. A versão traz validações de entrada em MP3, MP4, FLAC e Ogg, entre outros formatos. O adaptador conserva a recuperação do valor de faixa MP4 de 16 bits sem sinal. [Release upstream](https://github.com/taglib/taglib/releases/tag/v2.3.2)

O SDK em `out/product/taglib-2.3.2-msvc144/install` foi compilado a partir do asset oficial SHA-256 `3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc`. O build Release x64 usa MSVC 19.44, Windows SDK 10.0.26100.0 e CRT dinâmico, com zlib desabilitado. O recibo em `out/product/taglib-2.3.2-msvc144/receipt.json` registra as opções de configuração, o pacote-fonte e os hashes. A receita de runtime inclui cinco avisos com os termos do TagLib e do utf8cpp 4.2.0. [Aviso do TagLib](../packaging/windows/notices/taglib/NOTICE.txt)

O configure anterior rejeitou o SDK 2.3.2 pelo pin 2.3.1. Com o novo pin, o build dos alvos focados e o CTest passaram em 52 casos de metadados, incluindo capas e a faixa MP4, e uma jornada real de áudio. A inspeção do processo de áudio confirmou `tag.dll` SHA-256 `bdecdae7817d6a504cbbc63f04c467396f7e7c22fb2418d16bd411edb79bd191`. Logs e recibo do módulo em `out/product/r9/out/product/taglib-232-*`. A receita de empacotamento passou 25 testes após incluir os cinco avisos.

O build Release e a montagem Lite de `837ec6a` passaram. O runtime tem 572 arquivos com hashes conferidos, incluindo a DLL TagLib acima, FFmpeg `7.1.5-rcmusic2` e os cinco avisos em `notices/taglib/`. A conferência do instalador verificou a DLL e os avisos na seção obrigatória do plano gerado. O manifesto SHA-256 `90e11a87197e6d7e8560a4392203da3d258a0848344f600ebec0730f5037688e` e o recibo `taglib-verification.json` estão em `out/product/r9/out/product/taglib-232-runtime/`.

## Validação do incremento

Incremento posterior: a [receita de runtime](windows-runtime.md) e seus helpers estão em `packaging/windows`. O snapshot de produto não exige inputs M0; os encaminhadores históricos usam a mesma implementação. O guia separa checks sintéticos, montagem real e smoke, sem reatribuir evidências dos pacotes anteriores.

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
