# Candidato Qt

Shell experimental C++20/Qt Quick de M0.2. A stack continua apenas como candidata: este diretório não é o scaffold do produto e não aceita o ADR-0001.

## Estado

Os incrementos TDD materializados em `shell/` incluem o core C++20 puro, com portas para playback e integração de plataforma, o backend manual determinístico e o suporte Qt isolado para carregar a fixture congelada e expor a fila em um `QAbstractListModel`. O core cobre toggle play/pause, saltos com clamp, seek com preview/ack, preferências em memória e a decisão de close/tray. O backend reproduz os snapshots de 200 ms, transporte, clamp/pausa no fim e ack/rejeição de seek sem relógio ou áudio reais. O adapter Qt ancora os bytes de `fixture.v1.json` e `queue.v1.json` na cadeia protocolo → manifesto e preserva ordem, tipos e casos hostis da fila comum.

Ainda estão pendentes o presenter Qt, a UI QML, os temas, o tray concreto e as validações manuais. Não há áudio real, rede, leitura de biblioteca pessoal nem dependência Qt no core.

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

Para validar também o repository da fixture e o modelo da fila, use a mesma shell x64 e a toolchain Qt autenticada. Os caminhos da fixture, do manifesto e do protocolo são fechados pelo próprio scaffold e não são parâmetros substituíveis:

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
& $ctest --test-dir $build -C Release --output-on-failure
```

Configure com `M0_QT_SHELL_SUPPORT=OFF` — o padrão — continua compilando e testando o core sem localizar Qt nem ler a fixture.
