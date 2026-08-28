# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

Os incrementos TDD materializados em `shell/` incluem o core C++20 puro, com portas para playback e integração de plataforma, o backend manual determinístico e o suporte Qt isolado para carregar a fixture congelada e expor a fila em um `QAbstractListModel`. O modelo nasce vazio e aceita exatamente uma carga não vazia por reset, preservando a mesma identidade. O presenter `QObject` começa no estado vazio, preserva valores opcionais por propriedades `has…`, aplica explicitamente todos os defaults tipados e publica de forma atômica a fila e o snapshot falso derivados da fixture. Ele oferece ações semânticas de transporte e preferências, projeta metadata tipada da faixa atual e do contexto anterior e agenda um único tick preciso de 200 ms somente durante reprodução ou enquanto um seek aguarda ack/rejeição. O cenário falso de faixa indisponível interrompe qualquer seek pendente, para o timer e recupera o estado tocando pela ação estreita `skipUnavailable`, sem recarregar ou substituir o modelo de 1.000 itens. A entrada artificial usa somente o controle de cenário fechado do harness C++, acionável no executável de teste por `--m0-scenario=unavailable`; ela não aparece no metaobject nem na API QML e é rejeitada em builds com `BUILD_TESTING=OFF`. O core cobre toggle play/pause, saltos com clamp, seek com preview/ack, cancelamento de seek quando o backend deixa de ser transportável, preferências em memória e a decisão de close/tray. O backend reproduz os snapshots de 200 ms, transporte, clamp/pausa no fim e ack/rejeição de seek sem relógio ou áudio reais. O adapter Qt autentica os dois arquivos antes do parse, ancora seus bytes na cadeia protocolo → manifesto e projeta em tipos explícitos o contrato de apresentação, preferências, estados, detalhes, assets e fila, preservando ordem, nullabilidade e casos hostis. A seam de decodificação sem hash existe somente sob `src/` para testes semânticos causais e não faz parte da API pública.

A camada visual cria uma `ApplicationWindow` 960 × 640 responsiva até o mínimo 720 × 520. O módulo QML estático registra uma única vez `ShellUiContract` e `PlayerPresenter` como tipos não criáveis; o QML recebe somente essas instâncias controladas, sem filesystem, backend ou API genérica. “Abrir áudio” carrega a fila congelada em uma interação e a jornada mantém a mesma barra ao alternar entre vazio, tocando/pausado e erro recuperável. A barra compacta expõe progresso e tempos, saltos ±5 s, play/pause, navegação manual anterior/próxima, volume e mudo; seek por clique, drag ou teclado usa preview e exatamente uma tentativa de commit observada pelo teste, e o estado de erro preserva metadata contextual e oferece “Pular”. A fila lateral read-only usa os tokens congelados de 320–380 px, sobrepõe o conteúdo em 720 px e reserva espaço em 960 px. Sua `ListView` habilita reutilização com cache zero, uma única parada de Tab e delegates stateless; cada linha expõe ID estável, nome completo, seleção, indicador atual, duração e indisponibilidade, preservando markup como texto plano. Quick Test percorre os casos Unicode, markup, texto longo, indisponível e último item, verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação e cobre `Ctrl+L`, `Esc` e devolução de foco. Essa contagem não inclui todo o pool de reutilização nem o total de instanciações durante a execução. Botões e sliders possuem indicador de foco explícito, targets mínimos, IDs, nomes/roles e semântica `focusable`/`focused`; estados habilitado/desabilitado, ordem de Tab e limites sem sobreposição são verificados em 720 × 520. Textos de metadata usam formato plano, e capas decorativas não duplicam anúncios. Fixture, fila e SVGs são validados na configuração e fechados no executável sob aliases `qrc`; o bootstrap não incorpora caminhos do checkout. Qt Test verifica a boundary, o registro tipado, a ordem integral dos 1.000 IDs e os hashes/tamanhos finais; um smoke separado abre o executável real no erro e recupera pela ação QML “Pular”, preservando a barra e o modelo da fila. A árvore nativa por NVDA, escalas 100–200%, contraste alto, movimento reduzido e o stall externo de 100 ms continuam validações pendentes.

A navegação manual segue a [política experimental comum a M0.2/M0.3](../../../../docs/m0/README.md), sem alterar fixture ou protocolo. Anterior em posição maior que 3.000 ms e com seek reinicia por seek para zero; até 3.000 ms ou sem seek procura a ocorrência anterior disponível. Próxima pula indisponíveis no sentido seguinte, sem circular; não encontrar vizinho deixa o estado inalterado. Ambos os comandos são inertes em vazio/carregando/erro ou com seek em preview/aguardando ack. Uma troca começa em zero com a duração da linha de destino e preserva `Ready`/`Paused`/`Playing`, volume, mudo e identidade do modelo. EOF permanece `clamp_paused`. A recuperação “Pular” do cenário `queue-0999`, com contexto `queue-0017`, permanece separada e volta ao seed `queue-0017` em 84 s. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

O painel sincroniza seleção e rolagem com a ocorrência atual sem mover o foco do transporte. O título atual fica limitado à área disponível quando a fila está aberta. Quick Test cobre a preservação do foco durante a troca e os limites do título longo de `queue-0512`, sem alegar inspeção visual manual.

### Preferências/temas — fatia fechada em `ab87803`

A fatia implementou preferências/temas conforme a política comum da seção 6 do [registro M0](../../../../docs/m0/README.md), sem alterar a fixture/protocolo. O painel modal fica limitado ao conteúdo, mantendo o transporte visível, e usa Geral/Reprodução/Aparência com seleção exclusiva. Diálogo, botões, seletores, toggles e editores possuem roles explícitos; `Tab`/`Shift+Tab` permanecem no painel. Abrir fecha o painel da fila, seleciona Geral e foca a categoria; `Ctrl+,` abre/refoca sem alternar, e `Esc`/Fechar devolve foco ao botão `preferences`. Com dropdown aberto, o primeiro `Esc` fecha só a lista, preservando o modal. Atalhos de transporte/fila não atuam com o painel aberto. IDs e textos adicionais são centralizados no candidato conforme a tabela comum; volume/mudo não são duplicados, e Aplicativos Padrão fica para M0.9.

Os campos de salto são avaliados a cada tecla: inteiros ASCII válidos de 1–300 s são aplicados imediatamente e de modo independente; drafts inválidos mostram “Use um número inteiro entre 1 e 300.” e preservam o último valor válido do core. Um prefixo válido já pode ter sido aplicado antes de a edição ficar inválida; não há rollback automático. Fechar/reabrir descarta somente o draft e exibe o último valor válido em memória. A nota visível explica que preferências ficam somente em memória e sessão/fim da fila reais são futuros; EOF continua `clamp_paused`, sem concluir FR-042/FR-043/FR-044. Tema “Sistema” usa `QStyleHints::colorScheme`, desconhecido usa claro e os modos Claro/Escuro prevalecem; a resolução ocorre antes do QML e quando o esquema/preferência muda. Os 12 tokens da paleta congelada alcançam também a palette Basic e os sliders. Movimento “Sistema” no Windows consulta `SPI_GETCLIENTAREAANIMATION` somente para leitura; desconhecido/sem API usa reduzido, e a escolha Reduzido prevalece. Painel e feedback usam respectivamente 160 ms e 120 ms no modo normal e 0 ms no reduzido, sem animação contínua. As fontes oficiais e a separação entre APIs e políticas do experimento estão no registro M0.

Os checks automatizados desta fatia passaram: Red/Green do presenter, jornada Quick Test, suíte isolada de `QtSystemAppearance`, CTest Release estrito 58/58 em 7,25 s e core Qt OFF 51/51 com 473 asserções. Após o ajuste final dos delegates, Quick passou 7/7 em cada fator `QT_SCALE_FACTOR=1.5` e `2`, em modo estrito/offscreen; dez repetições do Quick e dez do smoke do executável passaram. Formato C++/QML, `qmllint`, contratos/hashes da fixture e markers também passaram.

Reds adicionais reproduziram contraste 1:1 no item destacado do seletor Basic e altura de 38 px abaixo do mínimo de 40 px. O delegate corrigido usa `Highlight`/`HighlightedText` ou `Base`/`Text` e altura preferida de 44 px que acomoda o conteúdo. A regressão também verifica `Esc` no dropdown mantendo o modal e `Tab`/`Shift+Tab` em Aparência; o Release completo acima já inclui essa correção, sem alegação de alto contraste nativo.

O build novo Qt ON com `BUILD_TESTING=OFF`, atualizado pelo rebuild final dos delegates, não contém testes ou controles artificiais e rejeita cenário indisponível/smoke com exit 1; o alias normal `--m0-scenario=empty` continua aceito. O default permaneceu vivo por 1,5 s sem output antes da limpeza dos PIDs; isso não prova saída graciosa. Não restaram processos de validação. A [evidência consolidada](../../../../docs/m0/README.md) registra a auditoria dos builds e os limites dos smokes.

No harness, `FixtureRepositoryTests` e `PlayerPresenterTests` agora usam `QGuiApplication` offscreen: com `QCoreApplication`, `QAbstractItemModelTester` exercitava papéis que geravam warnings de `QPixmap`. Essa correção é do ambiente de teste Qt e não adiciona Qt ao core.

### Alto contraste — implementação e evidência automatizada

A fatia HC está implementada sob a política comum da seção 7 do [registro M0](../../../../docs/m0/README.md), com checks automatizados e revisão de código aprovados. A paleta do sistema prevalece temporariamente sobre os três temas sem sobrescrever a preferência em memória; sair reaplica a escolha atual, e movimento continua independente. A nota `preferences-high-contrast-note`, com ID acessível explícito, explica essa precedência. `HighContrastPalette` projeta cinco cores opacas, validadas antes da publicação, sobre conteúdo, foco, hover, seleção e controles desabilitados; `TransportSlider` concentra a apresentação de progresso/volume. A scrollbar controlada mantém opacidade 1 em HC e usa feedback de 120/0 ms fora dele, sem fade residual no modo reduzido.

O adapter usa `QAccessibilityHints` para detectar o modo e `GetSysColor` no Windows para suas cores, com fallback `QPalette` não validado nativamente no Linux. A leitura inicial antecede o QML; notificações de esquema/contraste/paleta e mensagens Windows são coalescidas após o processamento Qt. A fixture e o protocolo continuam intocados.

A auditoria independente encontrou três P2, reproduzidos em Reds Quick sequenciais de 9 passagens/1 falha: handle focado usando `Highlight` sobre `Window`, `Accessible.id` ausente na nota HC e anel de foco do ComboBox com contraste aproximado de 1,09:1. Pares de cor e ID foram corrigidos com regressões; a revisão independente de código confirmou os achados resolvidos.

Checks confirmados nesta fatia, separados dos resultados históricos acima:

- Contrato C++: Red 12 passagens/6 falhas → Green 18/18, repetido três vezes; adapter: Red 17 passagens/16 falhas → Green 33/33, com 30 repetições por CTest.
- `tst_high_contrast.qml`: Red por método ausente e depois HC falso → Green; Quick completo 10/10 sem skips, também 10/10 em cada escala offscreen 1.5/2. Quick e smoke do executável passaram dez vezes cada.
- CTest Release estrito final, após os três ajustes da auditoria: 58/58 em 8,38 s; `qmllint` silencioso; formato de 36 C++/17 QML e diff check aprovados; fixture/markers com hashes inalterados. As escalas e as dez repetições de cada teste acima foram reexecutadas após esses ajustes.
- Core Qt OFF: 51/51, 473 asserções e auditoria de 113 comandos/cache/imports sem Qt. Rebuild incremental `BUILD_TESTING=OFF` pós-ajustes aprovado: zero testes, 153 comandos sem hooks/fontes de teste, nenhum executável de teste nem DLL Qt Test.

No build sem testes atualizado, o default estrito ficou vivo por 1,5 s sem output e foi encerrado pelo próprio PID, sem processos residuais; isso não prova saída graciosa. Cenário indisponível e sua combinação com smoke artificial foram rejeitados com exit 1 no build HC antes das três correções visuais, que não alteraram o parsing. O registro M0 detalha esses limites; não se trata de build novo.

Os testes usam paletas, leitores e notificações injetados: verificam a apresentação do candidato, não a leitura real de `GetSysColor`, a alternância de HC do Windows, ramos internos do Qt Basic ou Linux. Escalas offscreen não comprovam DPI real/NVDA, e não houve inspeção manual da UI nessa fatia HC. Os resultados de `ab87803` acima permanecem como histórico separado desta fatia.

M0.2 permanece em andamento. Validação nativa completa do pacote, medições, bandeja/alto contraste nativos e acessibilidade manual continuam pendentes; UX-006 não está encerrado. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida. A [observação posterior do pacote `63717e5`](../../../../docs/m0/evidence/m0-2-msvc-package.md) registrou abertura/pausa/fila/Preferências/tema, mas ficou parcial e não fecha os gates nativos. A prova funcional de virtualização não declara NFR-PERF-006 aprovado; o gate de stall exige o harness externo de M0.4, e a carga única da fixture em memória não comprova processamento incremental/cancelável de arquivos. Não há áudio real, rede, leitura de biblioteca pessoal nem dependência Qt no core.

## Bandeja e ciclo de vida

A [política comum da seção 8](../../../../docs/m0/README.md) está implementada no candidato Qt. `QtShellPlatform` usa `QSystemTrayIcon`/`QMenu`, com `QApplication` e Qt Widgets somente na borda do shell. O core continua C++ puro. O menu usa o transporte do presenter e expõe título atual/contextual, reproduzir/pausar, anterior/próxima, mostrar/ocultar e sair. Rótulos e avisos adicionais ficam centralizados em `LifecycleText`; o ícone reutiliza o SVG autenticado da fixture.

Fechar respeita a preferência; ocultar explicitamente a ignora; sair encerra uma única vez. O primeiro fechamento para bandeja oferece confirmação/cancelamento e um checkbox de aviso somente nesta execução. Sem bandeja observada, a janela é minimizada normalmente e disponibiliza uma explicação na restauração, inclusive se a capacidade mudar durante a tentativa de ocultar. O timer coarse de 1 s consulta disponibilidade somente enquanto a janela está oculta. O adapter preserva o último modo normal/maximizado/fullscreen e não alterna visibilidade em cliques repetidos no ícone. A API Windows do Qt retorna disponibilidade constante; isso não comprova presença do ícone ou recuperação do Explorer.

O novo smoke `--m0-smoke=tray-lifecycle`, exclusivo de `BUILD_TESTING=ON` e cenário `Empty`, exercita o executável real com capacidade de bandeja injetada e plataforma offscreen: menu, pausa, aviso QML, ocultação/restauração maximizada, navegação, perda de capacidade e saída. Sucesso exige código zero e janela fechada pelo caminho enfileirado normal; o watchdog só pode produzir falha. A trava de saída precede `window.close()` e `quit()`, evitando que o fechamento retorne à bandeja. Não há áudio, rede ou automação do desktop nesses testes.

Os checks de ciclo de vida incluem core/presenter, contrato do adapter, binding integrado, jornadas Quick e o smoke real. A suíte Release estrita passou 65/65; o core Qt OFF passou 55/55 com 1.849 asserções. Regressões cobrem perda tardia da bandeja, cancelamento/reabertura durante fade e foco de Preferências/fila concorrendo com o modal. Os detalhes e limites dos checks ficam na seção 8 do registro M0. A bandeja nativa, NVDA, DPI/HC reais, Linux e a validação nativa do pacote permanecem pendentes; o smoke local offscreen está registrado separadamente abaixo. Não há conclusão integral de FR-030/FR-031/FR-032.

## Checks de formato C++ e QML

Execute na raiz do repositório após o preflight autenticado, com PowerShell 7 e `rg` disponíveis. O caminho abaixo é o `clang-format` 19.1.1 do Visual Studio validado no host. O `qmlformat` 6.11.2 não oferece `--check`; o check compara sua saída padrão com o arquivo normalizado para LF, sem editar os arquivos. Seu `--dry-run` apenas informa settings e não substitui essa comparação.

```powershell
$formatRepo = (Resolve-Path .).Path
$formatSource = Join-Path $formatRepo 'experiments\m0\candidates\qt\shell'
$clangFormat = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'
$qmlFormat = Join-Path $formatRepo 'out\m0\toolchains\qt-6.11.2-msvc2022_64\bin\qmlformat.exe'
if (-not (Test-Path -LiteralPath $clangFormat -PathType Leaf) -or
    -not (Test-Path -LiteralPath $qmlFormat -PathType Leaf)) {
    throw 'Execute o preflight: ferramentas de formato ausentes.'
}

$cppFiles = @(rg --files $formatSource -g '*.cpp' -g '*.hpp')
if ($LASTEXITCODE -ne 0 -or $cppFiles.Count -eq 0) { throw 'Falha ao enumerar C++.' }
& $clangFormat --dry-run --Werror @cppFiles
if ($LASTEXITCODE -ne 0) { throw 'Formato C++ divergente.' }

$qmlFiles = @(rg --files $formatSource -g '*.qml')
if ($LASTEXITCODE -ne 0 -or $qmlFiles.Count -eq 0) { throw 'Falha ao enumerar QML.' }
foreach ($qmlFile in $qmlFiles) {
    $formattedLines = @(& $qmlFormat --newline unix $qmlFile)
    if ($LASTEXITCODE -ne 0) { throw "qmlformat falhou: $qmlFile" }
    $formatted = ($formattedLines -join "`n") + "`n"
    $original = (Get-Content -LiteralPath $qmlFile -Raw).Replace("`r`n", "`n").Replace("`r", "`n")
    if ($formatted -cne $original) { throw "Formato QML divergente: $qmlFile" }
}
Write-Output "Formato OK: $($cppFiles.Count) C++ e $($qmlFiles.Count) QML."
```

## Ambiente de build MSVC 14.44

O preflight Qt histórico permanece imutável. O [contrato de refresh](../../provisioning/qt-shell/msvc-refresh-contract.v1.json), instalado e validado conforme a [evidência de instalação](../../../../docs/m0/evidence/m0-2-msvc-installation.md), fixa separadamente MSVC 19.44.35228.0, linker/dumpbin 14.44.35228.0 e toolset 14.44.35207. Builds antigos com 19.43 não são aceitos pela receita atual; não reaproveite seus diretórios.

Após o preflight autenticado, abra uma sessão nova de PowerShell 7 x64 na raiz do repositório e selecione explicitamente a instância e o SDK:

```powershell
$m0VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\BuildTools'
Import-Module (Join-Path $m0VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $m0VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
$m0CompilerBin = Join-Path $m0VsRoot 'VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64'
$m0Compiler = Join-Path $m0CompilerBin 'cl.exe'
$m0Linker = Join-Path $m0CompilerBin 'link.exe'
$m0Ninja = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
```

A receita autentica CMake, Ninja, `windeployqt` e `qtpaths`, além das ferramentas MSVC, antes de executá-las e exige o ambiente MSVC/SDK esperado. Não corrige silenciosamente `INCLUDE`, `LIB` ou flags herdadas; use uma sessão limpa se houver divergência. O Ninja histórico autenticado continua sendo usado por path absoluto, sem depender de sua presença no `PATH` da nova instância. Isso não é autenticação transitiva de todo codegen/loader: `qmlimportscanner` não tem pin próprio no descriptor histórico, e todos os headers/libs do SDK não são individualmente atestados.

## Build e testes do core

Na sessão preparada acima, use um diretório próprio do novo compilador:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\core-msvc-14.44-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DM0_CATCH2_SOURCE_DIR=$catch2" "-DCMAKE_CXX_COMPILER=$m0Compiler" "-DCMAKE_LINKER=$m0Linker" "-DCMAKE_MAKE_PROGRAM=$m0Ninja"
& $cmake --build $build --target m0_qt_core_tests
& $ctest --test-dir $build -C Release --output-on-failure -R '^m0\.qt\.core\.'
```

O Catch2 precisa ser o source tree autenticado do preflight e o build permanece sob `out/m0`; nenhuma dependência é baixada durante configure ou build.

## Build e testes com suporte Qt

Para validar também o repository da fixture, o modelo da fila e a jornada QML, use a mesma shell x64 e a toolchain Qt autenticada. Os caminhos-fonte da fixture, do manifesto e do protocolo são fechados pelo próprio scaffold e não são parâmetros substituíveis; o executável consome cópias autenticadas em recursos `qrc`:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\support-msvc-14.44-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$qt = Join-Path $repo 'out\m0\toolchains\qt-6.11.2-msvc2022_64'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DM0_CATCH2_SOURCE_DIR=$catch2" -DM0_QT_SHELL_SUPPORT=ON "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_CXX_COMPILER=$m0Compiler" "-DCMAKE_LINKER=$m0Linker" "-DCMAKE_MAKE_PROGRAM=$m0Ninja"
& $cmake --build $build
& $cmake --build $build --target m0_qt_shell_ui_qmllint
& $ctest --test-dir $build -C Release --output-on-failure
```

No build do harness acima, que mantém `BUILD_TESTING=ON`, o cenário congelado de erro pode ser aberto manualmente no próprio executável e recuperado com “Pular”:

```powershell
$env:PATH = (Join-Path $qt 'bin') + ';' + $env:PATH
& (Join-Path $build 'm0_qt_shell.exe') --m0-scenario=unavailable
```

Para repetir apenas os contratos de bandeja e seu smoke sem abrir UI nativa:

```powershell
& $ctest --test-dir $build -C Release --output-on-failure -R '^m0\.qt\.(shell-platform(-binding)?|shell-executable\.tray-lifecycle)$'
```

Em um diretório novo configurado com `-DBUILD_TESTING=OFF`, os cenários/hooks de smoke são rejeitados, e não há executáveis de teste nem dependência do app em Qt Test. O build novo `out/m0/builds/qt-shell/tray-no-tests-20260827` comprovou isso; a observação de startup offscreen por 1,5 s usou encerramento forçado somente para cleanup, não como prova de saída normal.

Configure com `M0_QT_SHELL_SUPPORT=OFF` — o padrão — continua compilando e testando o core sem localizar Qt nem ler a fixture.

## Pacote local de avaliação

A receita em `package/` prepara somente uma pasta privada para M0.2, não um instalador nem uma release para distribuição. O executável contém os hooks de `BUILD_TESTING=ON`, mas o payload não inclui executáveis de teste ou Qt Test. O runtime C++ do host é um pré-requisito externo; nenhum redistributable é instalado ou copiado. A seção 9 do [registro M0](../../../../docs/m0/README.md) contém a política, proveniência e limitações.

O stage histórico `tray-34edef1-local-02`, derivado do shell de `34edef1`, passou na conferência de 1.378 arquivos/82.388.347 bytes e 69 PEs x64. A cópia para `relocação com espaços 34edef1-01` preservou o mesmo manifesto e payload. Naquela tentativa, o smoke isolado foi bloqueado por `RTSSHooks64.dll` injetado pelo RivaTuner; `error-recovery` do pacote não chegou a executar. Os smokes do build comum estavam verdes no CTest, mas não substituíam essa integração. Nenhuma configuração externa foi alterada e o processo do player foi limpo.

O stage posterior `tray-200b983-clean-01` foi gerado pela receita commitada em `200b983`, com worktree limpo e `recipe_dirty=false`. Seu manifesto é `a227bb2b23b7ec958b49318183ec399fd9c1e648e640a75e5fa7d06e5a97dc99`; o payload confere integralmente com o stage anterior. O build existente não precisou recompilar. Essa regeneração estática não executou o aplicativo.

Após `2cd898a`, zero processos RTSS foram encontrados e o mesmo runner passou nas duas jornadas offscreen do stage `tray-200b983-clean-01` e de sua nova cópia `relocação com espaços 200b983-01`. Saídas normais em código 0, logs vazios, captura exigida antes de 500 ms e manifesto íntegro antes/depois estão registrados na seção 9 do [registro M0](../../../../docs/m0/README.md). Somente módulos do pacote/System32 foram amostrados; a whitelist permaneceu inalterada. Desktop nativo, segurança/toolchain, máquina limpa e M0.2 continuam pendentes.

### Gerar e verificar

Use PowerShell 7.6.5 no ambiente **MSVC 14.44/SDK 10.0.22621.0** preparado acima, na raiz do repositório, após configurar o novo build Qt ON/Release. Fontes do shell, fixture e protocolo devem estar commitados e limpos. A receita identifica separadamente mudanças ainda não commitadas em si mesma ou no contrato de refresh (`recipe_dirty`); antes de medições, todo o worktree deve estar limpo. O nome do destino deve ser novo: não há opção de sobrescrita.

```powershell
$ErrorActionPreference = 'Stop'
$packageRepo = (Resolve-Path .).Path
$packageScripts = Join-Path $packageRepo 'experiments/m0/candidates/qt/package'
$packageBuild = Join-Path $packageRepo 'out/m0/builds/qt-shell/support-msvc-14.44-release'
$packageName = 'local-' + [guid]::NewGuid().ToString('N')
$package = & (Join-Path $packageScripts 'New-QtShellPackage.ps1') -BuildDirectory $packageBuild -Name $packageName
$package

Import-Module (Join-Path $packageScripts 'QtShellPackageManifest.psm1') -Force
Test-QtShellPackageManifest -PackageRoot $package.package_root -ManifestPath $package.manifest_path -ExpectedManifestSha256 $package.manifest_sha256 | Out-Null
```

O retorno contém o hash que identifica o manifesto gerado. Para verificar um pacote guardado ou copiado, use o hash registrado na evidência externa; recalcular um hash a partir de um manifesto desconhecido ou confiar somente no sidecar não autentica sua origem. Os diretórios precisam permanecer quiescentes durante geração, verificação e execução. `evidence/` guarda log de build, receipt MSVC, mapa de cópias e imports PE; `app/` contém o payload inventariado. O manifesto vincula o hash do receipt pós-build e mantém separados os hashes dos contratos histórico e de refresh. O validador de payload não revalida por si só os arquivos externos de `evidence/`.

### Smoke offscreen, sem paths do SDK

Feche o RivaTuner e outros injetores/overlays antes deste check. O runner rejeita módulos externos à pasta do pacote e ao Windows, exige Qt/plugins no payload e não altera aplicações externas ou configurações do SO. Ele cria ambiente filho mínimo e diretórios neutros de trabalho/perfil, usa `offscreen`, `QT_QUICK_BACKEND=software` e estilo Basic, sem herdar os paths Qt/QML do shell de desenvolvimento.

```powershell
$packageSmoke = & (Join-Path $packageScripts 'Test-QtShellPackageSmoke.ps1') -PackageRoot $package.package_root -ManifestPath $package.manifest_path -ExpectedManifestSha256 $package.manifest_sha256
$packageSmoke | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path (Split-Path $package.manifest_path) 'evidence/smoke-offscreen.v1.json') -Encoding utf8
```

O runner executa somente `tray-lifecycle` e `error-recovery`, em ordem, e só retorna aprovação se ambas terminarem com código 0, output vazio e captura dos módulos requeridos antes de 500 ms. O manifesto é validado antes de iniciar e novamente depois de ambas as jornadas. O timeout de cada jornada é 5 s; kill serve somente ao cleanup de falha. Snapshots são amostras, não um trace completo do loader. Guard local de erro impede diálogos de loader/crash na criação do processo e restaura as flags do caller. Isso não substitui bandeja/Explorer nativos, NVDA, HC/DPI reais, Linux, máquina limpa ou performance.

### Refresh MSVC na receita

Após a [primeira tentativa cancelada](../../../../docs/m0/evidence/m0-2-msvc-refresh.md), a [nova instalação autorizada](../../../../docs/m0/evidence/m0-2-msvc-installation.md) terminou com sucesso. A receita atual autentica o contrato fixado e os arquivos CMake/ferramentas antes e depois do build; falha em qualquer fase impede criar o stage. O manifesto usa a identidade pós-build, incluindo linker, ambiente e hashes separados do refresh. O pacote histórico não é substituído ou reclassificado. Testes sintéticos da orquestração não substituem compilação/linkagem, pacote real ou integração nativa.

A [evidência de integração](../../../../docs/m0/evidence/m0-2-msvc-package.md) registra 206/206 testes do empacotador, Release Qt 65/65, core Qt OFF 55/55 e o pacote diagnóstico com smoke 2/2. Depois do commit `63717e5`, o pacote `msvc-63717e5-clean-01` foi gerado com `recipe_dirty=false` e passou nas duas jornadas originais e nas duas relocadas. A tentativa nativa mostrou abertura/pausa/fila/Preferências/tema, mas as entradas foram pausadas ao detectar interação manual; foco acessível, saída, NVDA e HC/DPI não foram aprovados. A [regressão FreeType isolada](../../../../docs/m0/evidence/m0-2-freetype-backport.md) teve três falhas em nove casos no vendor original e Green 9/9 com o patch oficial, mas ainda não corrige QtGui/pacote. Segurança continua pendente; a reconstrução Qt exige nova identidade/configuração sem modificar o SDK histórico.

### Rebuild Qtbase autenticado

A [execução isolada de 2026-08-28](../../../../docs/m0/evidence/m0-2-qtbase-refresh.md) concluiu configure/build/install do Qtbase 6.11.2 com `5a280ec` em um `RunRoot` novo. O manifesto v2 `dc26c096…afbf6b` e o receipt `8a4e3c13…28619`, ancorado externamente, foram revalidados em outro processo junto com as 4.289 folhas físicas, os 15 PEs contratuais e `qminimal`. O SDK Qt oficial e `msvc-63717e5-clean-01` preservam seus bytes anteriores.

O passo posterior [compôs e autenticou o SDK Qt](../../../../docs/m0/evidence/m0-2-qt-composite-sdk.md) no run `f75eb9bea8280844c6d1a2b3f4`: contrato `f75eb9b…`, manifesto `221afa…`, receipt `30399b…` e 9.880 arquivos/2.400.301.667 bytes; o processo independente retornou `verified=True`. O SDK oficial, o contrato v1 e os builds/pacotes históricos permanecem preservados. A receita e o contrato `package-v2` estão implementados e congelados, mas ainda não foram usados para configurar, compilar, empacotar ou executar este candidato. O próximo incremento é executar essa receita contra a identidade composta com Windows SDK `10.0.26100.0`, seguido dos checks Release e smokes original/relocado. Até essa execução, não há prova nova de carregabilidade, ABI, pacote, máquina limpa, CRT, licenças/notices/SBOM ou redistribuição. `CHK-SEC`, M0.2 e os gates nativos continuam abertos; a interação desktop permanece pausada.

### Receita autenticada `package-v2` — execução real pendente

A receita congelada fica em `package-v2/` e exige Windows x64 e PowerShell 7.2 ou superior. Os exemplos abaixo são os comandos operacionais para a execução real futura; nenhum build, pacote ou smoke novo é alegado por esta documentação. Execute-os somente a partir do commit da receita, em um worktree limpo e numa nova sessão PowerShell. O hash do contrato é uma âncora externa literal obrigatória: não o recalcule a partir do contrato recebido e não o derive do sidecar.

Prepare as âncoras e somente os diretórios-pai. `BuildRoot`, `OutputRoot`, `ScratchRoot` e `SmokeWorkspaceRoot` devem ser filhos novos, ausentes, distintos e não sobrepostos sob `out/m0`:

```powershell
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess -or
    $PSVersionTable.PSVersion -lt [version]'7.2') {
    throw 'A receita package-v2 exige Windows x64 e PowerShell 7.2 ou superior.'
}

$packageV2Repo = (Resolve-Path .).Path
$packageV2Dir = Join-Path $packageV2Repo 'experiments\m0\candidates\qt\package-v2'
$packageV2Launcher = Join-Path $packageV2Dir 'Invoke-M0QtShellCompositePackage.ps1'
$packageV2Contract = Join-Path $packageV2Dir 'qt-shell-composite-package-contract.v2.json'
$compositeReceipt = Join-Path $packageV2Repo 'q\f75eb9bea8280844c6d1a2b3f4\evidence\qt-shell-composite-receipt.v2.json'

$packageV2ContractSha256 = 'cfc8ffd88c018798279af583be95f5dfe71ceb77254e6004c83cd5721894b72a'
$compositeReceiptSha256 = '30399b3099cf04bdd50d6f083d077e82a590afefeec0617fda89ef5266a1d63e'
$qtbaseReceiptSha256 = '8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619'

$packageV2Common = @{
    ContractPath = $packageV2Contract
    ExpectedContractSha256 = $packageV2ContractSha256
    CompositeReceiptPath = $compositeReceipt
    ExpectedCompositeReceiptSha256 = $compositeReceiptSha256
    ExpectedQtBaseReceiptSha256 = $qtbaseReceiptSha256
    RepositoryRoot = $packageV2Repo
}

$buildParent = Join-Path $packageV2Repo 'out\m0\builds\qt-shell\package-v2'
$packageParent = Join-Path $packageV2Repo 'out\m0\packages\qt-shell\package-v2'
$scratchParent = Join-Path $packageV2Repo 'out\m0\scratch\qt-shell\package-v2'
$smokeParent = Join-Path $packageV2Repo 'out\m0\smoke\qt-shell\package-v2'
$reportParent = Join-Path $packageV2Repo 'out\m0\reports\qt-shell\package-v2'
foreach ($parent in @($buildParent, $packageParent, $scratchParent, $smokeParent, $reportParent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
```

O modo **Build** autentica receita, receipts, fontes e ferramentas antes/depois, produz os builds Qt ON e core OFF Release, executa seus checks e publica um pacote novo:

```powershell
$buildId = [guid]::NewGuid().ToString('N')
$buildRoot = Join-Path $buildParent $buildId
$packageRoot = Join-Path $packageParent $buildId
$buildScratchRoot = Join-Path $scratchParent ('build-' + $buildId)

$packageV2Build = & $packageV2Launcher @packageV2Common `
    -BuildRoot $buildRoot `
    -OutputRoot $packageRoot `
    -ScratchRoot $buildScratchRoot
$packageV2Build
```

O modo **Relocate** primeiro autentica o receipt canônico do pacote de origem, copia seu inventário fechado para um root novo com espaços e acentos e autentica novamente o destino:

```powershell
$relocationId = [guid]::NewGuid().ToString('N')
$relocatedPackageRoot = Join-Path $packageParent ('relocação com espaços-' + $relocationId)
$relocationScratchRoot = Join-Path $scratchParent ('relocate-' + $relocationId)

$packageV2Relocated = & $packageV2Launcher @packageV2Common `
    -SourcePackageRoot $packageV2Build.package_root `
    -ReceiptPath $packageV2Build.receipt_path `
    -ExpectedReceiptSha256 $packageV2Build.receipt_sha256 `
    -OutputRoot $relocatedPackageRoot `
    -ScratchRoot $relocationScratchRoot
$packageV2Relocated
```

O modo **Smoke** exige o receipt canônico e seu hash externo capturado do Build. `ReportPath` deve estar dentro do repositório, fora do pacote, com diretório-pai já existente e arquivo-alvo ausente. Use roots e relatórios diferentes para original e relocado:

```powershell
$originalSmokeId = [guid]::NewGuid().ToString('N')
$originalSmokeWorkspace = Join-Path $smokeParent ('original-' + $originalSmokeId)
$originalReportPath = Join-Path $reportParent ('original-' + $originalSmokeId + '.json')
$packageV2OriginalSmoke = & $packageV2Launcher @packageV2Common `
    -PackageRoot $packageV2Build.package_root `
    -ReceiptPath $packageV2Build.receipt_path `
    -ExpectedReceiptSha256 $packageV2Build.receipt_sha256 `
    -SmokeWorkspaceRoot $originalSmokeWorkspace `
    -ReportPath $originalReportPath
$packageV2OriginalSmoke

$relocatedSmokeId = [guid]::NewGuid().ToString('N')
$relocatedSmokeWorkspace = Join-Path $smokeParent ('relocated-' + $relocatedSmokeId)
$relocatedReportPath = Join-Path $reportParent ('relocated-' + $relocatedSmokeId + '.json')
$packageV2RelocatedSmoke = & $packageV2Launcher @packageV2Common `
    -PackageRoot $packageV2Relocated.package_root `
    -ReceiptPath $packageV2Relocated.receipt_path `
    -ExpectedReceiptSha256 $packageV2Relocated.receipt_sha256 `
    -SmokeWorkspaceRoot $relocatedSmokeWorkspace `
    -ReportPath $relocatedReportPath
$packageV2RelocatedSmoke
```

Os testes sintéticos da receita não executam o build pesado nem substituem os checks reais. Rode cada arquivo em um processo limpo para evitar estado compartilhado entre módulos:

```powershell
$packageV2Tests = @(
    'CompositeBuild.Tests.ps1'
    'Contract.Tests.ps1'
    'PackageV2Production.Tests.ps1'
    'PackageV2ProductionExecution.Tests.ps1'
    'PackageV2Relocation.Tests.ps1'
    'PackageV2Smoke.Tests.ps1'
    'PackageV2Staging.Tests.ps1'
    'PackageV2Verifier.Tests.ps1'
)
foreach ($testFile in $packageV2Tests) {
    & pwsh -NoLogo -NoProfile -NonInteractive -File `
        (Join-Path $packageV2Dir "tests\$testFile")
    if ($LASTEXITCODE -ne 0) { throw "Falha nos testes package-v2: $testFile" }
}
```

Não há dependência deliberada em Pester ou PSScriptAnalyzer. O parser AST abaixo é somente um check de sintaxe PowerShell, não lint, e a ausência de PSScriptAnalyzer não deve ser apresentada como lint aprovado:

```powershell
$packageV2Scripts = @(Get-ChildItem -LiteralPath $packageV2Dir -Recurse -File |
    Where-Object { $_.Extension -in @('.ps1', '.psm1') })
foreach ($scriptFile in $packageV2Scripts) {
    $tokens = $null
    $parseErrors = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile(
        $scriptFile.FullName, [ref]$tokens, [ref]$parseErrors)
    if (@($parseErrors).Count -ne 0) {
        throw "Parser AST falhou: $($scriptFile.FullName): $($parseErrors -join '; ')"
    }
}
```

### Testes do empacotador

Os testes usam fixtures sintéticas e contratos locais autenticados, sem áudio, rede ou execução do player:

```powershell
foreach ($testFile in @('Builder.Tests.ps1', 'ToolchainRefresh.Tests.ps1', 'RefreshedBuild.Tests.ps1', 'Prerequisites.Tests.ps1', 'Manifest.Tests.ps1', 'Smoke.Tests.ps1')) {
    & pwsh -NoLogo -NoProfile -File (Join-Path $packageScripts "tests/$testFile")
    if ($LASTEXITCODE -ne 0) { throw "Falha nos testes: $testFile" }
}
```

Não há dependência em Pester ou PSScriptAnalyzer. Parser PowerShell, testes, revisão do diff e os checks nativos do shell fazem parte do fechamento; ausência de PSScriptAnalyzer não é reportada como lint aprovado. Arquivos de evidência e fixtures temporárias ficam sob `out/m0` e não são incluídos no Git.
