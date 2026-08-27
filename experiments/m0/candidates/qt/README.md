# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

Os incrementos TDD materializados em `shell/` incluem o core C++20 puro, com portas para playback e integração de plataforma, o backend manual determinístico e o suporte Qt isolado para carregar a fixture congelada e expor a fila em um `QAbstractListModel`. O modelo nasce vazio e aceita exatamente uma carga não vazia por reset, preservando a mesma identidade. O presenter `QObject` começa no estado vazio, preserva valores opcionais por propriedades `has…`, aplica explicitamente todos os defaults tipados e publica de forma atômica a fila e o snapshot falso derivados da fixture. Ele oferece ações semânticas de transporte e preferências, projeta metadata tipada da faixa atual e do contexto anterior e agenda um único tick preciso de 200 ms somente durante reprodução ou enquanto um seek aguarda ack/rejeição. O cenário falso de faixa indisponível interrompe qualquer seek pendente, para o timer e recupera o estado tocando pela ação estreita `skipUnavailable`, sem recarregar ou substituir o modelo de 1.000 itens. A entrada artificial usa somente o controle de cenário fechado do harness C++, acionável no executável de teste por `--m0-scenario=unavailable`; ela não aparece no metaobject nem na API QML e é rejeitada em builds com `BUILD_TESTING=OFF`. O core cobre toggle play/pause, saltos com clamp, seek com preview/ack, cancelamento de seek quando o backend deixa de ser transportável, preferências em memória e a decisão de close/tray. O backend reproduz os snapshots de 200 ms, transporte, clamp/pausa no fim e ack/rejeição de seek sem relógio ou áudio reais. O adapter Qt autentica os dois arquivos antes do parse, ancora seus bytes na cadeia protocolo → manifesto e projeta em tipos explícitos o contrato de apresentação, preferências, estados, detalhes, assets e fila, preservando ordem, nullabilidade e casos hostis. A seam de decodificação sem hash existe somente sob `src/` para testes semânticos causais e não faz parte da API pública.

A camada visual cria uma `ApplicationWindow` 960 × 640 responsiva até o mínimo 720 × 520. O módulo QML estático registra uma única vez `ShellUiContract` e `PlayerPresenter` como tipos não criáveis; o QML recebe somente essas instâncias controladas, sem filesystem, backend ou API genérica. “Abrir áudio” carrega a fila congelada em uma interação e a jornada mantém a mesma barra ao alternar entre vazio, tocando/pausado e erro recuperável. A barra compacta expõe progresso e tempos, saltos ±5 s, play/pause, navegação manual anterior/próxima, volume e mudo; seek por clique, drag ou teclado usa preview e exatamente uma tentativa de commit observada pelo teste, e o estado de erro preserva metadata contextual e oferece “Pular”. A fila lateral read-only usa os tokens congelados de 320–380 px, sobrepõe o conteúdo em 720 px e reserva espaço em 960 px. Sua `ListView` habilita reutilização com cache zero, uma única parada de Tab e delegates stateless; cada linha expõe ID estável, nome completo, seleção, indicador atual, duração e indisponibilidade, preservando markup como texto plano. Quick Test percorre os casos Unicode, markup, texto longo, indisponível e último item, verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação e cobre `Ctrl+L`, `Esc` e devolução de foco. Essa contagem não inclui todo o pool de reutilização nem o total de instanciações durante a execução. Botões e sliders possuem indicador de foco explícito, targets mínimos, IDs, nomes/roles e semântica `focusable`/`focused`; estados habilitado/desabilitado, ordem de Tab e limites sem sobreposição são verificados em 720 × 520. Textos de metadata usam formato plano, e capas decorativas não duplicam anúncios. Fixture, fila e SVGs são validados na configuração e fechados no executável sob aliases `qrc`; o bootstrap não incorpora caminhos do checkout. Qt Test verifica a boundary, o registro tipado, a ordem integral dos 1.000 IDs e os hashes/tamanhos finais; um smoke separado abre o executável real no erro e recupera pela ação QML “Pular”, preservando a barra e o modelo da fila. A árvore nativa por NVDA, escalas 100–200%, contraste alto, movimento reduzido e o stall externo de 100 ms continuam validações pendentes.

A navegação manual segue a [política experimental comum a M0.2/M0.3](../../../../docs/m0/README.md), sem alterar fixture ou protocolo. Anterior em posição maior que 3.000 ms e com seek reinicia por seek para zero; até 3.000 ms ou sem seek procura a ocorrência anterior disponível. Próxima pula indisponíveis no sentido seguinte, sem circular; não encontrar vizinho deixa o estado inalterado. Ambos os comandos são inertes em vazio/carregando/erro ou com seek em preview/aguardando ack. Uma troca começa em zero com a duração da linha de destino e preserva `Ready`/`Paused`/`Playing`, volume, mudo e identidade do modelo. EOF permanece `clamp_paused`. A recuperação “Pular” do cenário `queue-0999`, com contexto `queue-0017`, permanece separada e volta ao seed `queue-0017` em 84 s. CRUD, avanço automático por `ended`, fim da fila real e persistência não entram nesta fatia nem concluem FR-011/FR-016/FR-017.

O painel sincroniza seleção e rolagem com a ocorrência atual sem mover o foco do transporte. O título atual fica limitado à área disponível quando a fila está aberta. Quick Test cobre a preservação do foco durante a troca e os limites do título longo de `queue-0512`, sem alegar inspeção visual manual.

A fatia atual implementa preferências/temas conforme a política comum da seção 6 do [registro M0](../../../../docs/m0/README.md), sem alterar a fixture/protocolo. O painel modal fica limitado ao conteúdo, mantendo o transporte visível, e usa Geral/Reprodução/Aparência com seleção exclusiva. Diálogo, botões, seletores, toggles e editores possuem roles explícitos; `Tab`/`Shift+Tab` permanecem no painel. Abrir fecha o painel da fila, seleciona Geral e foca a categoria; `Ctrl+,` abre/refoca sem alternar, e `Esc`/Fechar devolve foco ao botão `preferences`. Com dropdown aberto, o primeiro `Esc` fecha só a lista, preservando o modal. Atalhos de transporte/fila não atuam com o painel aberto. IDs e textos adicionais são centralizados no candidato conforme a tabela comum; volume/mudo não são duplicados, e Aplicativos Padrão fica para M0.9.

Os campos de salto são avaliados a cada tecla: inteiros ASCII válidos de 1–300 s são aplicados imediatamente e de modo independente; drafts inválidos mostram “Use um número inteiro entre 1 e 300.” e preservam o último valor válido do core. Um prefixo válido já pode ter sido aplicado antes de a edição ficar inválida; não há rollback automático. Fechar/reabrir descarta somente o draft e exibe o último valor válido em memória. A nota visível explica que preferências ficam somente em memória e sessão/fim da fila reais são futuros; EOF continua `clamp_paused`, sem concluir FR-042/FR-043/FR-044. Tema “Sistema” usa `QStyleHints::colorScheme`, desconhecido usa claro e os modos Claro/Escuro prevalecem; a resolução ocorre antes do QML e quando o esquema/preferência muda. Os 12 tokens da paleta congelada alcançam também a palette Basic e os sliders. Movimento “Sistema” no Windows consulta `SPI_GETCLIENTAREAANIMATION` somente para leitura; desconhecido/sem API usa reduzido, e a escolha Reduzido prevalece. Painel e feedback usam respectivamente 160 ms e 120 ms no modo normal e 0 ms no reduzido, sem animação contínua. As fontes oficiais e a separação entre APIs e políticas do experimento estão no registro M0.

Os checks automatizados desta fatia passaram: Red/Green do presenter, jornada Quick Test, suíte isolada de `QtSystemAppearance`, CTest Release estrito 58/58 em 7,25 s e core Qt OFF 51/51 com 473 asserções. Após o ajuste final dos delegates, Quick passou 7/7 em cada fator `QT_SCALE_FACTOR=1.5` e `2`, em modo estrito/offscreen; dez repetições do Quick e dez do smoke do executável passaram. Formato C++/QML, `qmllint`, contratos/hashes da fixture e markers também passaram.

Reds adicionais reproduziram contraste 1:1 no item destacado do seletor Basic e altura de 38 px abaixo do mínimo de 40 px. O delegate corrigido usa `Highlight`/`HighlightedText` ou `Base`/`Text` e altura preferida de 44 px que acomoda o conteúdo. A regressão também verifica `Esc` no dropdown mantendo o modal e `Tab`/`Shift+Tab` em Aparência; o Release completo acima já inclui essa correção, sem alegação de alto contraste nativo.

O build novo Qt ON com `BUILD_TESTING=OFF`, atualizado pelo rebuild final dos delegates, não contém testes ou controles artificiais e rejeita cenário indisponível/smoke com exit 1; o alias normal `--m0-scenario=empty` continua aceito. O default permaneceu vivo por 1,5 s sem output antes da limpeza dos PIDs; isso não prova saída graciosa. Não restaram processos de validação. A [evidência consolidada](../../../../docs/m0/README.md) registra a auditoria dos builds e os limites dos smokes.

No harness, `FixtureRepositoryTests` e `PlayerPresenterTests` agora usam `QGuiApplication` offscreen: com `QCoreApplication`, `QAbstractItemModelTester` exercitava papéis que geravam warnings de `QPixmap`. Essa correção é do ambiente de teste Qt e não adiciona Qt ao core.

M0.2 permanece em andamento. Alto contraste fica para a próxima fatia; tray concreto, pacote, medições e validações manuais de acessibilidade continuam pendentes. A inspeção manual na fatia de fila virtualizada não foi realizada porque a sessão Computer Use foi interrompida; nenhuma nova verificação manual é alegada para navegação ou preferências/temas. Os testes controlados do adapter não comprovam a jornada nativa de mudança das configurações do SO. A prova funcional de virtualização não declara NFR-PERF-006 aprovado; o gate de stall exige o harness externo de M0.4, e a carga única da fixture em memória não comprova processamento incremental/cancelável de arquivos. Não há áudio real, rede, leitura de biblioteca pessoal nem dependência Qt no core.

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

## Build e testes do core

Execute primeiro o preflight autenticado na raiz do repositório. Depois, em um **Developer PowerShell for VS 2022** configurado para x64:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\core-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DM0_CATCH2_SOURCE_DIR=$catch2"
& $cmake --build $build --target m0_qt_core_tests
& $ctest --test-dir $build -C Release --output-on-failure -R '^m0\.qt\.core\.'
```

O Catch2 precisa ser o source tree autenticado do preflight e o build permanece sob `out/m0`; nenhuma dependência é baixada durante configure ou build.

## Build e testes com suporte Qt

Para validar também o repository da fixture, o modelo da fila e a jornada QML, use a mesma shell x64 e a toolchain Qt autenticada. Os caminhos-fonte da fixture, do manifesto e do protocolo são fechados pelo próprio scaffold e não são parâmetros substituíveis; o executável consome cópias autenticadas em recursos `qrc`:

```powershell
$repo = (Resolve-Path .).Path
$source = Join-Path $repo 'experiments\m0\candidates\qt\shell'
$build = Join-Path $repo 'out\m0\builds\qt-shell\support-release'
$catch2 = Join-Path $repo 'out\m0\toolchains\Catch2-3.15.3'
$qt = Join-Path $repo 'out\m0\toolchains\qt-6.11.2-msvc2022_64'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\CMake\bin\ctest.exe'

& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DM0_CATCH2_SOURCE_DIR=$catch2" -DM0_QT_SHELL_SUPPORT=ON "-DCMAKE_PREFIX_PATH=$qt"
& $cmake --build $build
& $cmake --build $build --target m0_qt_shell_ui_qmllint
& $ctest --test-dir $build -C Release --output-on-failure
```

No build do harness acima, que mantém `BUILD_TESTING=ON`, o cenário congelado de erro pode ser aberto manualmente no próprio executável e recuperado com “Pular”:

```powershell
& (Join-Path $build 'm0_qt_shell.exe') --m0-scenario=unavailable
```

Configure com `M0_QT_SHELL_SUPPORT=OFF` — o padrão — continua compilando e testando o core sem localizar Qt nem ler a fixture.
