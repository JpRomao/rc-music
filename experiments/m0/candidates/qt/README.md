# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

Os incrementos TDD materializados em `shell/` incluem o core C++20 puro, com portas para playback e integração de plataforma, o backend manual determinístico e o suporte Qt isolado para carregar a fixture congelada e expor a fila em um `QAbstractListModel`. O modelo nasce vazio e aceita exatamente uma carga não vazia por reset, preservando a mesma identidade. O presenter `QObject` começa no estado vazio, preserva valores opcionais por propriedades `has…`, aplica explicitamente todos os defaults tipados e publica de forma atômica a fila e o snapshot falso derivados da fixture. Ele oferece ações semânticas de transporte e preferências, projeta metadata tipada da faixa atual e do contexto anterior e agenda um único tick preciso de 200 ms somente durante reprodução ou enquanto um seek aguarda ack/rejeição. O cenário falso de faixa indisponível interrompe qualquer seek pendente, para o timer e recupera o estado tocando pela ação estreita `skipUnavailable`, sem recarregar ou substituir o modelo de 1.000 itens. A entrada artificial usa somente o controle de cenário fechado do harness C++, acionável no executável de teste por `--m0-scenario=unavailable`; ela não aparece no metaobject nem na API QML e é rejeitada em builds com `BUILD_TESTING=OFF`. O core cobre toggle play/pause, saltos com clamp, seek com preview/ack, cancelamento de seek quando o backend deixa de ser transportável, preferências em memória e a decisão de close/tray. O backend reproduz os snapshots de 200 ms, transporte, clamp/pausa no fim e ack/rejeição de seek sem relógio ou áudio reais. O adapter Qt autentica os dois arquivos antes do parse, ancora seus bytes na cadeia protocolo → manifesto e projeta em tipos explícitos o contrato de apresentação, preferências, estados, detalhes, assets e fila, preservando ordem, nullabilidade e casos hostis. A seam de decodificação sem hash existe somente sob `src/` para testes semânticos causais e não faz parte da API pública.

A camada visual cria uma `ApplicationWindow` 960 × 640 responsiva até o mínimo 720 × 520. O módulo QML estático registra uma única vez `ShellUiContract` e `PlayerPresenter` como tipos não criáveis; o QML recebe somente essas instâncias controladas, sem filesystem, backend ou API genérica. “Abrir áudio” carrega a fila congelada em uma interação e a jornada mantém a mesma barra ao alternar entre vazio, tocando/pausado e erro recuperável. A barra compacta expõe progresso e tempos, saltos ±5 s, play/pause, volume e mudo; seek por clique, drag ou teclado usa preview e exatamente uma tentativa de commit observada pelo teste, e o estado de erro preserva metadata contextual e oferece “Pular”. A fila lateral read-only usa os tokens congelados de 320–380 px, sobrepõe o conteúdo em 720 px e reserva espaço em 960 px. Sua `ListView` habilita reutilização com cache zero, uma única parada de Tab e delegates stateless; cada linha expõe ID estável, nome completo, seleção, indicador atual, duração e indisponibilidade, preservando markup como texto plano. Quick Test percorre os casos Unicode, markup, texto longo, indisponível e último item, verifica menos de 64 delegates associados a índices via `itemAtIndex` nos pontos testados da navegação e cobre `Ctrl+L`, `Esc` e devolução de foco. Essa contagem não inclui todo o pool de reutilização nem o total de instanciações durante a execução. Anterior/próxima permanecem visíveis e desabilitados até a fatia própria de navegação. Botões e sliders possuem indicador de foco explícito, targets mínimos, IDs, nomes/roles e semântica `focusable`/`focused`; estados habilitado/desabilitado, ordem de Tab e limites sem sobreposição são verificados em 720 × 520. Textos de metadata usam formato plano, e capas decorativas não duplicam anúncios. Fixture, fila e SVGs são validados na configuração e fechados no executável sob aliases `qrc`; o bootstrap não incorpora caminhos do checkout. Qt Test verifica a boundary, o registro tipado, a ordem integral dos 1.000 IDs e os hashes/tamanhos finais; um smoke separado abre o executável real no erro e recupera pela ação QML “Pular”, preservando a barra e o modelo da fila. A árvore nativa por NVDA, escalas 100–200%, contraste alto, movimento reduzido e o stall externo de 100 ms continuam validações pendentes.

M0.2 permanece em andamento. Ainda estão pendentes navegação anterior/próxima, preferências, aplicação dos temas, tray concreto, pacote, medições e validações manuais de acessibilidade. A inspeção manual da fila nesta fatia não foi realizada porque a sessão Computer Use foi interrompida. A prova funcional de virtualização não declara NFR-PERF-006 aprovado; o gate de stall exige o harness externo de M0.4, e a carga única da fixture em memória não comprova processamento incremental/cancelável de arquivos. Não há áudio real, rede, leitura de biblioteca pessoal nem dependência Qt no core.

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
